#include "iot/messaging/application_deployment_service.h"
#include "iot/ui/screen_manager.h"

#include "fake_mosquitto_library.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace iot {
namespace messaging {
namespace {

class ApplicationDeploymentServiceTest : public ::testing::Test {
protected:
  ApplicationDeploymentServiceTest()
      : m_screenManager(tests::testActiveDisplay(), std::make_unique<tests::RecordingRenderBackend>(), 16U),
        m_pythonApplicationManager(m_screenManager, tests::testActiveDisplay(), tests::testConnectedDisplays(),
                                   m_systemInformationProvider, m_fileDownloader, 256U * 1024U) {}

  void TearDown() override {
    if (m_applicationDeploymentService) {
      m_applicationDeploymentService->stop();
    }
    m_pythonApplicationManager.stop();
    m_screenManager.stop();
  }

  void createApplicationDeploymentService() {
    MqttApplicationReceiverSettings mqttReceiverSettings;
    mqttReceiverSettings.deviceId                  = "test-device";
    mqttReceiverSettings.brokerHost                = "mqtt.example.test";
    mqttReceiverSettings.brokerPort                = 1884U;
    mqttReceiverSettings.keepAliveSeconds          = 45U;
    mqttReceiverSettings.maximumMessageSizeInBytes = 4096U;

    ApplicationDeploymentServiceSettings deploymentServiceSettings;
    deploymentServiceSettings.mqttReceiverSettings              = std::move(mqttReceiverSettings);
    deploymentServiceSettings.maximumQueuedApplicationMessages  = 2U;
    deploymentServiceSettings.maximumPythonSourceSizeInBytes    = 1024U;
    deploymentServiceSettings.temporaryApplicationRootDirectory = m_temporaryDirectory.path();
    deploymentServiceSettings.maximumRememberedDeployments      = 4U;

    m_applicationDeploymentService = std::make_unique<ApplicationDeploymentService>(
        std::move(deploymentServiceSettings), m_pythonApplicationManager, m_mqttClientApi);
  }

  tests::TemporaryDirectory                     m_temporaryDirectory;
  ui::ScreenManager                             m_screenManager;
  tests::TestSystemInformationProvider          m_systemInformationProvider;
  tests::TestFileDownloader                     m_fileDownloader;
  python::PythonApplicationManager              m_pythonApplicationManager;
  tests::FakeMqttClientApi                      m_mqttClientApi;
  std::unique_ptr<ApplicationDeploymentService> m_applicationDeploymentService;
};

TEST_F(ApplicationDeploymentServiceTest, StartsAndStopsItsMqttReceiver) {
  createApplicationDeploymentService();

  m_applicationDeploymentService->start();

  EXPECT_EQ(m_mqttClientApi.connectedHost, "mqtt.example.test");
  EXPECT_EQ(m_mqttClientApi.connectedPort, 1884);
  EXPECT_EQ(m_mqttClientApi.connectedKeepAliveSeconds, 45);

  m_applicationDeploymentService->stop();
  EXPECT_TRUE(m_mqttClientApi.networkLoopWasStopped);
  EXPECT_TRUE(m_mqttClientApi.clientWasDestroyed);
  EXPECT_TRUE(m_mqttClientApi.libraryWasCleanedUp);
}

TEST_F(ApplicationDeploymentServiceTest, ReturnsWithoutProcessingWhenNoMessageIsWaiting) {
  createApplicationDeploymentService();

  m_applicationDeploymentService->waitForAndProcessOneMessage(std::chrono::milliseconds(0));

  EXPECT_TRUE(m_mqttClientApi.publishedPayload.empty());
}

TEST_F(ApplicationDeploymentServiceTest, QueuesMessagesUntilTheCallingThreadProcessesThem) {
  createApplicationDeploymentService();
  m_applicationDeploymentService->start();

  m_mqttClientApi.deliverMessage("iot/devices/test-device/applications/install",
                                 R"json({"transfer_id":"transfer-42","device_id":"wrong-device"})json");

  // The MQTT callback only puts the message in the queue. No deployment status
  // is published until the main thread asks the service to process it.
  EXPECT_TRUE(m_mqttClientApi.publishedPayload.empty());

  m_applicationDeploymentService->waitForAndProcessOneMessage(std::chrono::milliseconds(0));

  EXPECT_EQ(m_mqttClientApi.publishedTopic, "iot/devices/test-device/applications/status/transfer-42");
  EXPECT_NE(m_mqttClientApi.publishedPayload.find("\"rejected\""), std::string::npos);
}

} // namespace
} // namespace messaging
} // namespace iot
