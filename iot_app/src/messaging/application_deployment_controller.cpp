#include "iot/messaging/application_deployment_controller.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace iot {
namespace messaging {

ApplicationDeploymentController::ApplicationDeploymentController(
    std::string deviceId, ApplicationDeploymentMessageParser messageParser,
    python::TemporaryPythonApplicationInstaller &applicationInstaller,
    python::PythonApplicationManager &applicationManager, IMqttApplicationReceiver &mqttApplicationReceiver,
    std::size_t maximumRememberedDeployments)
    : m_deviceId(std::move(deviceId)), m_messageParser(std::move(messageParser)),
      m_applicationInstaller(applicationInstaller), m_applicationManager(applicationManager),
      m_mqttApplicationReceiver(mqttApplicationReceiver), m_maximumRememberedDeployments(maximumRememberedDeployments) {
  if (m_deviceId.empty() || m_maximumRememberedDeployments == 0U) {
    throw std::invalid_argument("Application deployment controller settings are incomplete");
  }
}

void ApplicationDeploymentController::process(const ReceivedApplicationMessage &receivedMessage) {
  const auto possibleTransferId = m_messageParser.tryReadTransferId(receivedMessage.payload);
  if (!possibleTransferId) {
    IOT_LOG_WARNING(m_logger, "Rejected an MQTT application message without a safe transfer ID; payloadBytes=",
                    receivedMessage.payload.size());
    return;
  }
  IOT_LOG_DEBUG(m_logger, "Processing deployment transferId=", *possibleTransferId,
                ", payloadBytes=", receivedMessage.payload.size());

  for (const auto &rememberedStatus : m_rememberedFinalStatuses) {
    if (rememberedStatus.transferId != *possibleTransferId) {
      continue;
    }
    // QoS 1 can deliver the same message more than once. Send the saved answer
    // again instead of installing and starting the app a second time.
    IOT_LOG_DEBUG(m_logger, "Received duplicate transferId=", *possibleTransferId,
                  "; publishing remembered state=", rememberedStatus.deploymentState);
    m_mqttApplicationReceiver.publishStatus(rememberedStatus);
    return;
  }

  ApplicationDeploymentStatus deploymentStatus{*possibleTransferId, "received", "", "Message received by IoT App"};
  m_mqttApplicationReceiver.publishStatus(deploymentStatus);

  ApplicationDeploymentRequest deploymentRequest;
  try {
    deploymentRequest                = m_messageParser.parse(receivedMessage.payload, m_deviceId);
    deploymentStatus.applicationId   = deploymentRequest.applicationId;
    deploymentStatus.deploymentState = "validating";
    deploymentStatus.message         = "Source size and SHA-256 are valid";
    IOT_LOG_INFO(m_logger, "Validated application id=", deploymentRequest.applicationId, ", name='",
                 deploymentRequest.applicationName, "', transferId=", deploymentRequest.transferId,
                 ", sourceBytes=", deploymentRequest.sourceCode.size());
    m_mqttApplicationReceiver.publishStatus(deploymentStatus);
  } catch (const std::exception &error) {
    IOT_LOG_WARNING(m_logger, "Rejected deployment transferId=", *possibleTransferId, ": ", error.what());
    deploymentStatus.deploymentState = "rejected";
    deploymentStatus.message         = error.what();
    publishAndRememberFinalStatus(deploymentStatus);
    return;
  }

  python::PythonApplication externalApplication;
  try {
    externalApplication              = m_applicationInstaller.installApplication(deploymentRequest);
    deploymentStatus.deploymentState = "starting";
    deploymentStatus.message         = "Temporary application is valid and is starting";
    m_mqttApplicationReceiver.publishStatus(deploymentStatus);
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(m_logger, "Could not install deployment transferId=", deploymentRequest.transferId,
                  ", applicationId=", deploymentRequest.applicationId, ": ", error.what());
    deploymentStatus.deploymentState = "failed";
    deploymentStatus.message         = error.what();
    publishAndRememberFinalStatus(deploymentStatus);
    return;
  }

  const auto previousExternalApplicationDirectory = m_activeExternalApplicationInstallDirectory;
  const auto activationResult = m_applicationManager.activateExternalApplication(externalApplication);
  if (!previousExternalApplicationDirectory.empty()) {
    m_applicationInstaller.removeInstalledApplication(previousExternalApplicationDirectory);
  }

  if (activationResult.externalApplicationIsRunning) {
    m_activeExternalApplicationInstallDirectory = externalApplication.packageDirectory;
    deploymentStatus.deploymentState            = "started";
    deploymentStatus.message                    = "External application started successfully";
    IOT_LOG_INFO(m_logger, "Application id=", deploymentRequest.applicationId,
                 ", transferId=", deploymentRequest.transferId, " is running from ",
                 m_activeExternalApplicationInstallDirectory);
  } else {
    m_applicationInstaller.removeInstalledApplication(externalApplication.packageDirectory);
    m_activeExternalApplicationInstallDirectory.clear();
    deploymentStatus.deploymentState = "failed";
    deploymentStatus.message         = activationResult.failureReason;
    IOT_LOG_ERROR(m_logger, "Application id=", deploymentRequest.applicationId,
                  ", transferId=", deploymentRequest.transferId, " failed to start: ", activationResult.failureReason);
  }
  publishAndRememberFinalStatus(deploymentStatus);
}

void ApplicationDeploymentController::publishAndRememberFinalStatus(
    const ApplicationDeploymentStatus &deploymentStatus) {
  IOT_LOG_DEBUG(m_logger, "Publishing final deployment state; transferId=", deploymentStatus.transferId,
                ", applicationId=", deploymentStatus.applicationId, ", state=", deploymentStatus.deploymentState);
  m_mqttApplicationReceiver.publishStatus(deploymentStatus);
  m_rememberedFinalStatuses.push_back(deploymentStatus);
  while (m_rememberedFinalStatuses.size() > m_maximumRememberedDeployments) {
    m_rememberedFinalStatuses.pop_front();
  }
}

} // namespace messaging
} // namespace iot
