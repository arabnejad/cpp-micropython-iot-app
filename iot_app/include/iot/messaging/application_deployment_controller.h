#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_message.h"
#include "iot/messaging/application_message_queue.h"
#include "iot/messaging/imqtt_application_receiver.h"
#include "iot/python/python_application_manager.h"
#include "iot/python/temporary_python_application_installer.h"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>

namespace iot {
namespace messaging {

/**
 * Processes application deployments from validation through activation.
 *
 * Only the main thread calls this class. This keeps all MicroPython startup and
 * shutdown work on the thread that created the interpreter.
 */
class ApplicationDeploymentController {
public:
  ApplicationDeploymentController(std::string deviceId, ApplicationDeploymentMessageParser messageParser,
                                  python::TemporaryPythonApplicationInstaller &applicationInstaller,
                                  python::PythonApplicationManager            &applicationManager,
                                  IMqttApplicationReceiver                    &mqttApplicationReceiver,
                                  std::size_t                                  maximumRememberedDeployments);

  // Uses shared deployment services; copying and moving are disabled.
  ApplicationDeploymentController(const ApplicationDeploymentController &)            = delete;
  ApplicationDeploymentController &operator=(const ApplicationDeploymentController &) = delete;
  ApplicationDeploymentController(ApplicationDeploymentController &&)                 = delete;
  ApplicationDeploymentController &operator=(ApplicationDeploymentController &&)      = delete;

  /** Handles one queued MQTT message and publishes progress and final status. */
  void process(const ReceivedApplicationMessage &receivedMessage);

private:
  void publishAndRememberFinalStatus(const ApplicationDeploymentStatus &deploymentStatus);

  logging::Logger                              logger_{"ApplicationDeploymentController"};
  std::string                                  deviceId_;
  ApplicationDeploymentMessageParser           messageParser_;
  python::TemporaryPythonApplicationInstaller &applicationInstaller_;
  python::PythonApplicationManager            &applicationManager_;
  IMqttApplicationReceiver                    &mqttApplicationReceiver_;
  std::size_t                                  maximumRememberedDeployments_{0};
  std::deque<ApplicationDeploymentStatus>      rememberedFinalStatuses_;
  std::filesystem::path                        activeExternalApplicationInstallDirectory_;
};

} // namespace messaging
} // namespace iot
