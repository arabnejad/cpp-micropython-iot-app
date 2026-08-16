#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_controller.h"
#include "iot/messaging/application_message_queue.h"
#include "iot/messaging/mqtt_application_receiver.h"

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace iot {
namespace messaging {

namespace internal {
class IMqttClientApi;
}

/* Settings shared by the deployment receiver, queue, and controller. */
struct ApplicationDeploymentServiceSettings {
  MqttApplicationReceiverSettings mqttReceiverSettings;
  std::size_t                     maximumQueuedApplicationMessages{0U};
  std::size_t                     maximumPythonSourceSizeInBytes{0U};
  std::filesystem::path           temporaryApplicationRootDirectory;
  std::size_t                     maximumRememberedDeployments{0U};
};

/*
 * Owns the components that receive and process MQTT application deployments.
 *
 * The MQTT receiver checks the topic and message size, then puts the message
 * in the bounded queue from its network thread. The main thread calls
 * waitForAndProcessOneMessage(), which removes one message and gives it to the
 * deployment controller. Parsing, file installation, and Python application
 * switching therefore remain on the main thread.
 */
class ApplicationDeploymentService {
public:
  ApplicationDeploymentService(ApplicationDeploymentServiceSettings deploymentServiceSettings,
                               python::PythonApplicationManager    &pythonApplicationManager,
                               internal::IMqttClientApi            &mqttClientApi);

  ApplicationDeploymentService(const ApplicationDeploymentService &)            = delete;
  ApplicationDeploymentService &operator=(const ApplicationDeploymentService &) = delete;
  ApplicationDeploymentService(ApplicationDeploymentService &&)                 = delete;
  ApplicationDeploymentService &operator=(ApplicationDeploymentService &&)      = delete;

  /* Starts the MQTT receiver and its network thread. */
  void start();

  /* Stops the MQTT receiver. Calling this more than once is safe. */
  void stop() noexcept;

  /* Waits for one received message and processes it on the calling thread. */
  void waitForAndProcessOneMessage(std::chrono::milliseconds maximumWaitTime);

private:
  logging::Logger                 m_logger{"ApplicationDeploymentService"};
  ApplicationMessageQueue         m_applicationMessageQueue;
  MqttApplicationReceiver         m_mqttApplicationReceiver;
  ApplicationDeploymentController m_deploymentController;
};

} // namespace messaging
} // namespace iot
