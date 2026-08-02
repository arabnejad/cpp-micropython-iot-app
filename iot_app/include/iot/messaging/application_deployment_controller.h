#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_message.h"
#include "iot/messaging/application_message_queue.h"
#include "iot/messaging/imqtt_application_receiver.h"
#include "iot/messaging/temporary_python_application_installer.h"
#include "iot/python/python_application_manager.h"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>

namespace iot {
namespace messaging {

/*
 * Handles a received application from validation through startup.
 *
 * Replies accepted after installation, before stopping the current app. Python
 * errors then appear on the device's emergency screen and in its log, rather
 * than as another deployment reply.
 *
 * The controller owns the message parser and temporary installer used for
 * received applications. PythonApplicationManager remains separate because it
 * owns the currently running application.
 *
 * It is called only by the main thread because MicroPython must start and stop
 * on the thread that owns the interpreter.
 */
class ApplicationDeploymentController {
public:
  ApplicationDeploymentController(std::string deviceId, std::size_t maximumPythonSourceSizeInBytes,
                                  std::filesystem::path             temporaryApplicationRootDirectory,
                                  python::PythonApplicationManager &applicationManager,
                                  IMqttApplicationReceiver         &mqttApplicationReceiver,
                                  std::size_t                       maximumRememberedDeployments);

  // Uses shared deployment services; copying and moving are disabled.
  ApplicationDeploymentController(const ApplicationDeploymentController &)            = delete;
  ApplicationDeploymentController &operator=(const ApplicationDeploymentController &) = delete;
  ApplicationDeploymentController(ApplicationDeploymentController &&)                 = delete;
  ApplicationDeploymentController &operator=(ApplicationDeploymentController &&)      = delete;

  /* Processes one queued MQTT message and publishes its status updates. */
  void process(const ReceivedApplicationMessage &receivedMessage);

private:
  void publishAndRememberFinalStatus(const ApplicationDeploymentStatus &deploymentStatus);

  logging::Logger                         m_logger{"ApplicationDeploymentController"};
  std::string                             m_deviceId;
  ApplicationDeploymentMessageParser      m_messageParser;
  TemporaryPythonApplicationInstaller     m_applicationInstaller;
  python::PythonApplicationManager       &m_applicationManager;
  IMqttApplicationReceiver               &m_mqttApplicationReceiver;
  std::size_t                             m_maximumRememberedDeployments{0};
  std::deque<ApplicationDeploymentStatus> m_rememberedFinalStatuses;
  std::filesystem::path                   m_activeExternalApplicationInstallDirectory;
};

} // namespace messaging
} // namespace iot
