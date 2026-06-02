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
      : temporaryApplicationInstaller_(pythonApplicationLoader_, temporaryDirectory_.path()),
        recordingRenderBackend_(std::make_unique<tests::RecordingRenderBackend>()),
        screenManager_(tests::testActiveDisplay(), std::move(recordingRenderBackend_), 16U),
        pythonApplicationRunner_(screenManager_, tests::testActiveDisplay(), displayManager_,
                                 systemInformationProvider_, 256U * 1024U),
        pythonApplicationManager_(pythonApplicationRunner_, screenManager_, tests::testActiveDisplay()) {}

  void SetUp() override {
    screenManager_.start();
  }

  void TearDown() override {
    pythonApplicationManager_.stop();
    screenManager_.stop();
  }

  std::unique_ptr<ApplicationDeploymentController>
  createDeploymentController(std::size_t rememberedDeploymentCapacity = 4U) {
    return std::make_unique<ApplicationDeploymentController>("test-device", ApplicationDeploymentMessageParser(1024U),
                                                             temporaryApplicationInstaller_, pythonApplicationManager_,
                                                             mqttApplicationReceiver_, rememberedDeploymentCapacity);
  }

  tests::TemporaryDirectory                      temporaryDirectory_;
  python::PythonApplicationLoader                pythonApplicationLoader_{1024U};
  python::TemporaryPythonApplicationInstaller    temporaryApplicationInstaller_;
  std::unique_ptr<tests::RecordingRenderBackend> recordingRenderBackend_;
  ui::ScreenManager                              screenManager_;
  tests::TestDisplayManager                      displayManager_;
  tests::TestSystemInformationProvider           systemInformationProvider_;
  python::PythonApplicationRunner                pythonApplicationRunner_;
  python::PythonApplicationManager               pythonApplicationManager_;
  RecordingMqttApplicationReceiver               mqttApplicationReceiver_;
};

TEST_F(ApplicationDeploymentControllerTest, InstallsAndStartsAValidatedExternalApplication) {
  auto deploymentController = createDeploymentController();

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(pythonApplicationManager_.state(), python::ApplicationState::ExternalApplication);
  EXPECT_EQ(pythonApplicationManager_.activeScreenName(), "External app");
  ASSERT_EQ(mqttApplicationReceiver_.publishedStatuses.size(), 4U);
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "started");
}

TEST_F(ApplicationDeploymentControllerTest, PublishesARejectionAndRemembersTheRejectedTransfer) {
  auto                             deploymentController = createDeploymentController(1U);
  const ReceivedApplicationMessage rejectedMessage{R"json({"transfer_id":"bad-transfer","device_id":"wrong"})json"};

  deploymentController->process(rejectedMessage);
  ASSERT_EQ(mqttApplicationReceiver_.publishedStatuses.size(), 2U);
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "rejected");

  deploymentController->process(rejectedMessage);
  ASSERT_EQ(mqttApplicationReceiver_.publishedStatuses.size(), 3U);
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "rejected");
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

  ASSERT_EQ(mqttApplicationReceiver_.publishedStatuses.size(), 4U);
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "failed");
  EXPECT_FALSE(std::filesystem::exists(temporaryDirectory_.path() / "transfer-42"));
}

TEST_F(ApplicationDeploymentControllerTest, RemovesThePreviousExternalApplicationWhenAReplacementStarts) {
  auto deploymentController = createDeploymentController(1U);
  deploymentController->process({validDeploymentMessageJson});
  const std::string replacementDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");

  deploymentController->process({replacementDeploymentJson});

  EXPECT_FALSE(std::filesystem::exists(temporaryDirectory_.path() / "transfer-42"));
  EXPECT_TRUE(std::filesystem::exists(temporaryDirectory_.path() / "transfer-43"));
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "started");
}

TEST_F(ApplicationDeploymentControllerTest, ProcessesATransferAgainAfterItsRememberedStatusIsRemoved) {
  auto deploymentController = createDeploymentController(1U);
  deploymentController->process({validDeploymentMessageJson});
  const std::string secondDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");
  deploymentController->process({secondDeploymentJson});
  const std::size_t statusesBeforeRepeatingFirstTransfer = mqttApplicationReceiver_.publishedStatuses.size();

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.size(), statusesBeforeRepeatingFirstTransfer + 4U);
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "started");
}

TEST_F(ApplicationDeploymentControllerTest, RejectsAnEmptyDeviceId) {
  EXPECT_THROW(ApplicationDeploymentController({}, ApplicationDeploymentMessageParser(1024U),
                                               temporaryApplicationInstaller_, pythonApplicationManager_,
                                               mqttApplicationReceiver_, 1U),
               std::invalid_argument);
}

TEST_F(ApplicationDeploymentControllerTest, RejectsAZeroRememberedDeploymentCapacity) {
  EXPECT_THROW(ApplicationDeploymentController("test-device", ApplicationDeploymentMessageParser(1024U),
                                               temporaryApplicationInstaller_, pythonApplicationManager_,
                                               mqttApplicationReceiver_, 0U),
               std::invalid_argument);
}

TEST_F(ApplicationDeploymentControllerTest, PublishesFailureWhenTheTemporaryApplicationCannotBeInstalled) {
  std::filesystem::permissions(temporaryDirectory_.path(),
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                               std::filesystem::perm_options::replace);
  auto deploymentController = createDeploymentController(1U);

  deploymentController->process({validDeploymentMessageJson});

  std::filesystem::permissions(temporaryDirectory_.path(), std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  ASSERT_EQ(mqttApplicationReceiver_.publishedStatuses.size(), 3U);
  EXPECT_EQ(mqttApplicationReceiver_.publishedStatuses.back().deploymentState, "failed");
}

TEST_F(ApplicationDeploymentControllerTest, IgnoresAMessageWithoutASafeTransferId) {
  auto deploymentController = createDeploymentController();

  deploymentController->process({R"json({"transfer_id":"../unsafe"})json"});

  EXPECT_TRUE(mqttApplicationReceiver_.publishedStatuses.empty());
}

TEST(ApplicationDeploymentMessageParserTest, RejectsAnUnsafeTransferIdBeforeDeploymentServicesAreNeeded) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  EXPECT_FALSE(deploymentMessageParser.tryReadTransferId(R"json({"transfer_id":"../unsafe"})json").has_value());
}

} // namespace
} // namespace messaging
} // namespace iot
