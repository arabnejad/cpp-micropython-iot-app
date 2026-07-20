#include "iot/messaging/application_deployment_controller.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

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

class MockMqttApplicationReceiver : public IMqttApplicationReceiver {
public:
  MOCK_METHOD(void, publishStatus, (const ApplicationDeploymentStatus &deploymentStatus), (override));
};

std::string replaceEveryOccurrence(std::string text, const std::string &oldValue, const std::string &newValue) {
  std::size_t matchPosition = 0U;
  while ((matchPosition = text.find(oldValue, matchPosition)) != std::string::npos) {
    text.replace(matchPosition, oldValue.size(), newValue);
    matchPosition += newValue.size();
  }
  return text;
}

// Keep the Python source readable in tests. The parser still receives real
// Base64 data, its byte count, and a matching SHA-256, just as with the sender.
std::string createDeploymentMessageForSource(const std::string &sourceCode) {
  unsigned char sourceDigest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(sourceCode.data()), sourceCode.size(), sourceDigest);
  std::string sourceSha256;
  const char *hexadecimalDigits = "0123456789abcdef";
  for (unsigned char digestByte : sourceDigest) {
    sourceSha256 += hexadecimalDigits[digestByte >> 4U];
    sourceSha256 += hexadecimalDigits[digestByte & 15U];
  }
  std::string encodedSource(4U * ((sourceCode.size() + 2U) / 3U) + 1U, '\0');
  const int   encodedLength =
      EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encodedSource.data()),
                      reinterpret_cast<const unsigned char *>(sourceCode.data()), static_cast<int>(sourceCode.size()));
  encodedSource.resize(encodedLength);
  std::string deploymentJson = replaceEveryOccurrence(
      validDeploymentMessageJson, "03e693d9f2f687e0f40e36a8df7fcb4d1c22974012b7c2a55c000eb30f305824", sourceSha256);
  deploymentJson = replaceEveryOccurrence(deploymentJson, "cHJpbnQoJ2hlbGxvJykK", encodedSource);
  return replaceEveryOccurrence(deploymentJson, "\"size_bytes\":15",
                                "\"size_bytes\":" + std::to_string(sourceCode.size()));
}

class ApplicationDeploymentControllerTest : public ::testing::Test {
protected:
  ApplicationDeploymentControllerTest()
      : m_temporaryApplicationInstaller(m_temporaryDirectory.path()),
        m_recordingRenderBackend(std::make_unique<tests::RecordingRenderBackend>()),
        m_recordingRenderBackendView(m_recordingRenderBackend.get()),
        m_screenManager(tests::testActiveDisplay(), std::move(m_recordingRenderBackend), 16U),
        m_pythonApplicationManager(m_screenManager, tests::testActiveDisplay(), tests::testConnectedDisplays(),
                                   m_systemInformationProvider, m_fileDownloader, 256U * 1024U) {}

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

  void startDefaultApplication() {
    python::PythonApplication defaultApplication;
    defaultApplication.applicationId   = "default";
    defaultApplication.applicationName = "Default app";
    defaultApplication.entryPointPath  = "main.py";
    defaultApplication.sourceCode      = "value = 1\n";
    m_pythonApplicationManager.startDefaultApplication(defaultApplication);
  }

  bool emergencyScreenContains(const std::string &expectedText) {
    return tests::waitUntil([&] {
      std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
      return m_recordingRenderBackendView->lastErrorScreenText.find(expectedText) != std::string::npos;
    });
  }

  tests::TemporaryDirectory                      m_temporaryDirectory;
  python::TemporaryPythonApplicationInstaller    m_temporaryApplicationInstaller;
  std::unique_ptr<tests::RecordingRenderBackend> m_recordingRenderBackend;
  tests::RecordingRenderBackend                 *m_recordingRenderBackendView;
  ui::ScreenManager                              m_screenManager;
  tests::TestSystemInformationProvider           m_systemInformationProvider;
  tests::TestFileDownloader                      m_fileDownloader;
  python::PythonApplicationManager               m_pythonApplicationManager;
  RecordingMqttApplicationReceiver               m_mqttApplicationReceiver;
};

TEST_F(ApplicationDeploymentControllerTest, AcceptsAValidatedApplicationBeforeStoppingTheCurrentAppOrRunningPython) {
  startDefaultApplication();
  ::testing::StrictMock<MockMqttApplicationReceiver> mqttApplicationReceiver;
  ApplicationDeploymentController deploymentController("test-device", ApplicationDeploymentMessageParser(1024U),
                                                       m_temporaryApplicationInstaller, m_pythonApplicationManager,
                                                       mqttApplicationReceiver, 4U);
  ::testing::InSequence           statusOrder;
  EXPECT_CALL(mqttApplicationReceiver,
              publishStatus(::testing::Field(&ApplicationDeploymentStatus::deploymentState, "received")));
  EXPECT_CALL(mqttApplicationReceiver,
              publishStatus(::testing::Field(&ApplicationDeploymentStatus::deploymentState, "validating")));
  EXPECT_CALL(mqttApplicationReceiver,
              publishStatus(::testing::Field(&ApplicationDeploymentStatus::deploymentState, "accepted")))
      .WillOnce([&](const ApplicationDeploymentStatus &deploymentStatus) {
        EXPECT_EQ(deploymentStatus.message, "Application received and ready to execute");
        EXPECT_EQ(deploymentStatus.applicationId, "external");
        EXPECT_EQ(deploymentStatus.transferId, "transfer-42");
        EXPECT_TRUE(std::filesystem::is_regular_file(m_temporaryDirectory.path() / "transfer-42" / "main.py"));
        EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::DefaultApplication);
        EXPECT_EQ(m_pythonApplicationManager.activeScreenName(), "Default app");
        EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 1U);
      });

  deploymentController.process({validDeploymentMessageJson});

  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::ExternalApplication);
  EXPECT_EQ(m_pythonApplicationManager.activeScreenName(), "External app");
}

TEST_F(ApplicationDeploymentControllerTest, RepeatsAcceptanceWithoutRestartingAnAlreadyProcessedApplication) {
  auto deploymentController = createDeploymentController();

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::ExternalApplication);
  EXPECT_EQ(m_pythonApplicationManager.activeScreenName(), "External app");
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");

  deploymentController->process({validDeploymentMessageJson});

  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 4U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
  EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 1U);
}

TEST_F(ApplicationDeploymentControllerTest, PublishesARejectionAndRemembersTheRejectedTransfer) {
  startDefaultApplication();
  auto                             deploymentController = createDeploymentController(1U);
  const ReceivedApplicationMessage rejectedMessage{R"json({"transfer_id":"bad-transfer","device_id":"wrong"})json"};

  deploymentController->process(rejectedMessage);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 2U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "rejected");
  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::DefaultApplication);

  deploymentController->process(rejectedMessage);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "rejected");
}

TEST_F(ApplicationDeploymentControllerTest, KeepsAcceptanceAndShowsAnEmergencyWhenDownloadCleanupFails) {
  auto deploymentController          = createDeploymentController();
  m_fileDownloader.clearErrorMessage = "download cleanup failed";

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::EmergencyScreen);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
  EXPECT_TRUE(emergencyScreenContains("download cleanup failed"));
  EXPECT_FALSE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-42"));
}

TEST_F(ApplicationDeploymentControllerTest, KeepsAcceptanceAfterAStartupFailureAndDoesNotRetryDuplicateDelivery) {
  auto       deploymentController = createDeploymentController(1U);
  const auto brokenDeploymentJson = createDeploymentMessageForSource("raise RuntimeError('startup failed')\n");

  deploymentController->process({brokenDeploymentJson});

  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::EmergencyScreen);
  EXPECT_TRUE(emergencyScreenContains("RuntimeError: startup failed"));
  EXPECT_FALSE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-42"));

  deploymentController->process({brokenDeploymentJson});

  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 4U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
  EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 1U);
  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::EmergencyScreen);
}

TEST_F(ApplicationDeploymentControllerTest, AcceptsSourceWithASyntaxErrorAndShowsTheCompilerErrorOnTheDevice) {
  auto deploymentController = createDeploymentController();

  deploymentController->process({createDeploymentMessageForSource("def broken(:\n")});

  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::EmergencyScreen);
  EXPECT_TRUE(emergencyScreenContains("SyntaxError"));
  EXPECT_FALSE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-42"));

  const auto replacementDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");
  deploymentController->process({replacementDeploymentJson});

  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::ExternalApplication);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
}

TEST_F(ApplicationDeploymentControllerTest, DoesNotPublishAnotherStatusWhenAScheduledCallbackFails) {
  auto       deploymentController = createDeploymentController();
  const auto deploymentJson       = createDeploymentMessageForSource("from iot import scheduler\n"
                                                                           "def fail():\n"
                                                                           "    raise RuntimeError('callback failed')\n"
                                                                           "scheduler.every(1, fail)\n");
  deploymentController->process({deploymentJson});
  ASSERT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::ExternalApplication);

  ASSERT_TRUE(tests::waitUntil([&] {
    m_pythonApplicationManager.runScheduledCallbacks();
    return m_pythonApplicationManager.state() == python::ApplicationState::EmergencyScreen;
  }));

  EXPECT_TRUE(emergencyScreenContains("RuntimeError: callback failed"));
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
}

TEST_F(ApplicationDeploymentControllerTest, RemovesThePreviousExternalApplicationWhenAReplacementStarts) {
  auto deploymentController = createDeploymentController(1U);
  deploymentController->process({validDeploymentMessageJson});
  const std::string replacementDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");

  deploymentController->process({replacementDeploymentJson});

  EXPECT_FALSE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-42"));
  EXPECT_TRUE(std::filesystem::exists(m_temporaryDirectory.path() / "transfer-43"));
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
}

TEST_F(ApplicationDeploymentControllerTest, ProcessesATransferAgainAfterItsRememberedStatusIsRemoved) {
  auto deploymentController = createDeploymentController(1U);
  deploymentController->process({validDeploymentMessageJson});
  const std::string secondDeploymentJson =
      replaceEveryOccurrence(validDeploymentMessageJson, "transfer-42", "transfer-43");
  deploymentController->process({secondDeploymentJson});
  const std::size_t statusesBeforeRepeatingFirstTransfer = m_mqttApplicationReceiver.publishedStatuses.size();

  deploymentController->process({validDeploymentMessageJson});

  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), statusesBeforeRepeatingFirstTransfer + 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "accepted");
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
  startDefaultApplication();
  std::filesystem::permissions(m_temporaryDirectory.path(),
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                               std::filesystem::perm_options::replace);
  auto deploymentController = createDeploymentController(1U);

  deploymentController->process({validDeploymentMessageJson});

  std::filesystem::permissions(m_temporaryDirectory.path(), std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  ASSERT_EQ(m_mqttApplicationReceiver.publishedStatuses.size(), 3U);
  EXPECT_EQ(m_mqttApplicationReceiver.publishedStatuses.back().deploymentState, "failed");
  EXPECT_EQ(m_pythonApplicationManager.state(), python::ApplicationState::DefaultApplication);
  EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 1U);
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
