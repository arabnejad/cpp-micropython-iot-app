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
    : deviceId_(std::move(deviceId)), messageParser_(std::move(messageParser)),
      applicationInstaller_(applicationInstaller), applicationManager_(applicationManager),
      mqttApplicationReceiver_(mqttApplicationReceiver), maximumRememberedDeployments_(maximumRememberedDeployments) {
  if (deviceId_.empty() || maximumRememberedDeployments_ == 0U) {
    throw std::invalid_argument("Application deployment controller settings are incomplete");
  }
}

void ApplicationDeploymentController::process(const ReceivedApplicationMessage &receivedMessage) {
  const auto possibleTransferId = messageParser_.tryReadTransferId(receivedMessage.payload);
  if (!possibleTransferId) {
    IOT_LOG_WARNING(logger_, "Rejected an MQTT application message without a safe transfer ID; payloadBytes=",
                    receivedMessage.payload.size());
    return;
  }
  IOT_LOG_DEBUG(logger_, "Processing deployment transferId=", *possibleTransferId,
                ", payloadBytes=", receivedMessage.payload.size());

  for (const auto &rememberedStatus : rememberedFinalStatuses_) {
    if (rememberedStatus.transferId != *possibleTransferId) {
      continue;
    }
    // QoS 1 can deliver the same message more than once. Send the saved answer
    // again instead of installing and starting the app a second time.
    IOT_LOG_DEBUG(logger_, "Received duplicate transferId=", *possibleTransferId,
                  "; publishing remembered state=", rememberedStatus.deploymentState);
    mqttApplicationReceiver_.publishStatus(rememberedStatus);
    return;
  }

  ApplicationDeploymentStatus deploymentStatus{*possibleTransferId, "received", "", "Message received by IoT App"};
  mqttApplicationReceiver_.publishStatus(deploymentStatus);

  ApplicationDeploymentRequest deploymentRequest;
  try {
    deploymentRequest                = messageParser_.parse(receivedMessage.payload, deviceId_);
    deploymentStatus.applicationId   = deploymentRequest.applicationId;
    deploymentStatus.deploymentState = "validating";
    deploymentStatus.message         = "Source size and SHA-256 are valid";
    IOT_LOG_INFO(logger_, "Validated application id=", deploymentRequest.applicationId, ", name='",
                 deploymentRequest.applicationName, "', transferId=", deploymentRequest.transferId,
                 ", sourceBytes=", deploymentRequest.sourceCode.size());
    mqttApplicationReceiver_.publishStatus(deploymentStatus);
  } catch (const std::exception &error) {
    IOT_LOG_WARNING(logger_, "Rejected deployment transferId=", *possibleTransferId, ": ", error.what());
    deploymentStatus.deploymentState = "rejected";
    deploymentStatus.message         = error.what();
    publishAndRememberFinalStatus(deploymentStatus);
    return;
  }

  python::PythonApplication externalApplication;
  try {
    externalApplication              = applicationInstaller_.installAndLoad(deploymentRequest);
    deploymentStatus.deploymentState = "starting";
    deploymentStatus.message         = "Temporary application is valid and is starting";
    mqttApplicationReceiver_.publishStatus(deploymentStatus);
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(logger_, "Could not install deployment transferId=", deploymentRequest.transferId,
                  ", applicationId=", deploymentRequest.applicationId, ": ", error.what());
    deploymentStatus.deploymentState = "failed";
    deploymentStatus.message         = error.what();
    publishAndRememberFinalStatus(deploymentStatus);
    return;
  }

  const auto previousExternalApplicationDirectory = activeExternalApplicationInstallDirectory_;
  const auto activationResult = applicationManager_.activateExternalApplication(externalApplication);
  if (!previousExternalApplicationDirectory.empty()) {
    applicationInstaller_.removeInstalledApplication(previousExternalApplicationDirectory);
  }

  if (activationResult.externalApplicationIsRunning) {
    activeExternalApplicationInstallDirectory_ = externalApplication.packageDirectory;
    deploymentStatus.deploymentState           = "started";
    deploymentStatus.message                   = "External application started successfully";
    IOT_LOG_INFO(logger_, "Application id=", deploymentRequest.applicationId,
                 ", transferId=", deploymentRequest.transferId, " is running from ",
                 activeExternalApplicationInstallDirectory_);
  } else {
    applicationInstaller_.removeInstalledApplication(externalApplication.packageDirectory);
    activeExternalApplicationInstallDirectory_.clear();
    deploymentStatus.deploymentState = "failed";
    deploymentStatus.message         = activationResult.failureReason;
    IOT_LOG_ERROR(logger_, "Application id=", deploymentRequest.applicationId,
                  ", transferId=", deploymentRequest.transferId, " failed to start: ", activationResult.failureReason);
  }
  publishAndRememberFinalStatus(deploymentStatus);
}

void ApplicationDeploymentController::publishAndRememberFinalStatus(
    const ApplicationDeploymentStatus &deploymentStatus) {
  IOT_LOG_DEBUG(logger_, "Publishing final deployment state; transferId=", deploymentStatus.transferId,
                ", applicationId=", deploymentStatus.applicationId, ", state=", deploymentStatus.deploymentState);
  mqttApplicationReceiver_.publishStatus(deploymentStatus);
  rememberedFinalStatuses_.push_back(deploymentStatus);
  while (rememberedFinalStatuses_.size() > maximumRememberedDeployments_) {
    rememberedFinalStatuses_.pop_front();
  }
}

} // namespace messaging
} // namespace iot
