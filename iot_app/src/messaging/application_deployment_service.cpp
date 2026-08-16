#include "iot/messaging/application_deployment_service.h"

#include "messaging/internal/imqtt_client_api.h"

#include <utility>

namespace iot {
namespace messaging {

ApplicationDeploymentService::ApplicationDeploymentService(
    ApplicationDeploymentServiceSettings deploymentServiceSettings,
    python::PythonApplicationManager &pythonApplicationManager, internal::IMqttClientApi &mqttClientApi)
    : m_applicationMessageQueue(deploymentServiceSettings.maximumQueuedApplicationMessages),
      m_mqttApplicationReceiver(deploymentServiceSettings.mqttReceiverSettings, m_applicationMessageQueue,
                                mqttClientApi),
      m_deploymentController(deploymentServiceSettings.mqttReceiverSettings.deviceId,
                             deploymentServiceSettings.maximumPythonSourceSizeInBytes,
                             std::move(deploymentServiceSettings.temporaryApplicationRootDirectory),
                             pythonApplicationManager, m_mqttApplicationReceiver,
                             deploymentServiceSettings.maximumRememberedDeployments) {}

void ApplicationDeploymentService::start() {
  IOT_LOG_DEBUG(m_logger, "Starting the application deployment service");
  m_mqttApplicationReceiver.start();
}

void ApplicationDeploymentService::stop() noexcept {
  IOT_LOG_DEBUG(m_logger, "Stopping the application deployment service");
  m_mqttApplicationReceiver.stop();
}

void ApplicationDeploymentService::waitForAndProcessOneMessage(std::chrono::milliseconds maximumWaitTime) {
  const auto receivedApplicationMessage = m_applicationMessageQueue.waitAndPopMessage(maximumWaitTime);
  if (!receivedApplicationMessage) {
    return;
  }

  IOT_LOG_DEBUG(m_logger, "Passing one queued MQTT application to the deployment controller; payloadBytes=",
                receivedApplicationMessage->payload.size());
  m_deploymentController.process(*receivedApplicationMessage);
}

} // namespace messaging
} // namespace iot
