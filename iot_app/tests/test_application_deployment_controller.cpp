#include "iot/messaging/application_deployment_controller.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <vector>

namespace iot {
namespace messaging {
namespace {

constexpr const char *validDeploymentMessageJson = R"json({
  "message_type":"install_single_file_application",
  "transfer_id":"transfer-42",
  "device_id":"test-device",
  "application":{"id":"external","name":"External app","entry_point":"main.py"},
  "source":{"encoding":"base64","size_bytes":15,"sha256":"03e693d9f2f687e0f40e36a8df7fcb4d1c22974012b7c2a55c000eb30f305824","content":"cHJpbnQoJ2hlbGxvJykK"}
})json";

class RecordingMqttApplicationReceiver final : public IMqttApplicationReceiver {
public:
  void publishStatus(const ApplicationDeploymentStatus &deploymentStatus) override {
    publishedStatuses.push_back(deploymentStatus);
  }

  std::vector<ApplicationDeploymentStatus> publishedStatuses;
};

std::string replaceEveryOccurrence(std::string text, const std::string &oldValue, const std::string &newValue) {
  std::size_t matchPosition = 0U;
  while ((matchPosition = text.find(oldValue, matchPosition)) != std::string::npos) {
    text.replace(matchPosition, oldValue.size(), newValue);
    matchPosition += newValue.size();
  }
  return text;
}

class ApplicationDeploymentControllerTest : public ::testing::Test {
protected:
  ApplicationDeploymentControllerTest()
      : m_temporaryApplicationInstaller(m_temporaryDirectory.path()),
        m_recordingRenderBackend(std::make_unique<tests::RecordingRenderBackend>()),
        m_screenManager(tests::testActiveDisplay(), std::move(m_recordingRenderBackend), 16U),
        m_pythonApplicationManager(m_screenManager, tests::testActiveDisplay(), tests::testConnectedDisplays(),
                                   m_systemInformationProvider, 256U * 1024U) {}

  void SetUp() override {
    m_screenManager.start();
  }

  void TearDown() override {
    m_pythonApplicationManager.stop();
    m_screenManager.stop();
  }

  std::unique_ptr<ApplicationDeploymentController>
  createDeploymentController(std::size_t rememberedDeploymentCapacity = 4U) {
    return std::make_unique<ApplicationDeploymentController>(
        "test-device", ApplicationDeploymentMessageParser(1024U), m_temporaryApplicationInstaller,
        m_pythonApplicationManager, m_mqttApplicationReceiver, rememberedDeploymentCapacity);
  }

  tests::TemporaryDirectory                      m_temporaryDirectory;
  python::TemporaryPythonApplicationInstaller    m_temporaryApplicationInstaller;
  std::unique_ptr<tests::RecordingRenderBackend> m_recordingRenderBackend;
  ui::ScreenManager                              m_screenManager;
  tests::TestSystemInformationProvider           m_systemInformationProvider;
  python::PythonApplicationManager               m_pythonApplicationManager;
  RecordingMqttApplicationReceiver               m_mqttApplicationReceiver;
};

TEST_F(ApplicationDeploymentControllerTest, InstallsAndStartsAValidatedExternalApplication) {
  auto deploymentController = createDeploymentController();

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::ExternalApplication);
  EXPECT_EQ(m_pythonApplicationManager.activeScreenName(), "External app");
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 4U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "started");
}

TEST_F(ApplicationDeploymentControllerTest, PublishesARejectionAndRemembersTheRejectedTransfer) {
  auto                             deploymentController = createDeploymentController(1U);
  const ReceivedApplicationMessage rejectedMessage{R"json({"transfer_id":"bad-transfer","device_id":"wrong"})json"};

  deploymentController->process(rejectedMessage);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 2U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "rejected");

  deploymentController->process(rejectedMessage);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "rejected");
}

TEST_F(ApplicationDeploymentControllerTest, RemovesAnApplicationThatFailsDuringStartupAndPublishesFailure) {
  auto        deploymentController = createDeploymentController(1U);
  std::string brokenDeploymentJson = validDeploymentMessageJson;
  brokenDeploymentJson             = replaceEveryOccurrence(std::move(brokenDeploymentJson),
                                                            "03e693d9f2f687e0f40e36a8df7fcb4d1c22974012b7c2a55c000eb30f305824",
                                                            "c253d5e6e97cf11c97bdf75eb4582c0fcedbb045923bc602a5d6323ca5869e78");
  brokenDeploymentJson             = replaceEveryOccurrence(std::move(brokenDeploymentJson), "cHJpbnQoJ2hlbGxvJykK",
                                                            "cmFpc2UgUnVudGltZUVycm9yKCdmYWlsJykK");
  brokenDeploymentJson =
      replaceEveryOccurrence(std::move(brokenDeploymentJson), "\"size_bytes\":15", "\"size_bytes\":27");

  deploymentController->process({brokenDeploymentJson});

  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 4U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "failed");
  EXPECT_FALSE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-42"));
}

TEST_F(ApplicationDeploymentControllerTest, RemovesThePreviousExternalApplicationWhenAReplacementStarts) {
  auto deploymentController = createDeploymentController(1U);
  deploymentController->process({validDeploymentMessageJson});
  const std::string replacementDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");

  deploymentController->process({replacementDeploymentJson});

  EXPECT_FALSE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-42"));
  EXPECT_TRUE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-43"));
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "started");
}

TEST_F(ApplicationDeploymentControllerTest, ProcessesATransferAgainAfterItsRememberedStatusIsRemoved) {
  auto deploymentController = createDeploymentController(1U);
  deploymentController->process({validDeploymentMessageJson});
  const std::string secondDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");
  deploymentController->process({secondDeploymentJson});
  const std::size_t statusesBeforeRepeatingFirstTransfer = m_mqttApplicationReceiver.publishedStatuses.size();

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), statusesBeforeRepeatingFirstTransfer + 4U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "started");
}

TEST_F(ApplicationDeploymentControllerTest, RejectsAnEmptyDeviceId) {
  EXPECT_THROW(ApplicationDeploymentController({}, ApplicationDeploymentMessageParser(1024U),
                                               m_temporaryApplicationInstaller, m_pythonApplicationManager,
                                               m_mqttApplicationReceiver, 1U),
               std::invalid_argument);
}

TEST_F(ApplicationDeploymentControllerTest, RejectsAZeroRememberedDeploymentCapacity) {
  EXPECT_THROW(ApplicationDeploymentController("test-device", ApplicationDeploymentMessageParser(1024U),
                                               m_temporaryApplicationInstaller, m_pythonApplicationManager,
                                               m_mqttApplicationReceiver, 0U),
               std::invalid_argument);
}

TEST_F(ApplicationDeploymentControllerTest, PublishesFailureWhenTheTemporaryApplicationCannotBeInstalled) {
  std::filesystem::permissions(m_temporaryDirectory.path(),
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                               std::filesystem::perm_options::replace);
  auto deploymentController = createDeploymentController(1U);

  deploymentController->process({validDeploymentMessageJson});

  std::filesystem::permissions(m_temporaryDirectory.path(), std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "failed");
}

TEST_F(ApplicationDeploymentControllerTest, IgnoresAMessageWithoutASafeTransferId) {
  auto deploymentController = createDeploymentController();

  deploymentController->process({R"json({"transfer_id":"../unsafe"})json"});

  EXPECT_TRUE(m_mqttApplicationReceiver.publishedStatuses.empty());
}

TEST(ApplicationDeploymentMessageParserTest, RejectsAnUnsafeTransferIdBeforeDeploymentServicesAreNeeded) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  EXPECT_FALSE(deploymentMessageParser.tryReadTransferId(R"json({"transfer_id":"../unsafe"})json").has_value());
}

} // namespace
} // namespace messaging
} // namespace iot
