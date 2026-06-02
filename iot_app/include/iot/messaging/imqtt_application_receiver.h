#pragma once

#include "iot/messaging/application_deployment_message.h"

namespace iot {
namespace messaging {

/**
 * The part of `MqttApplicationReceiver` used by the deployment controller.
 *
 * The deployment controller only publishes status messages. Broker connection,
 * subscription, and network callback work stays in `MqttApplicationReceiver`.
 */
class IMqttApplicationReceiver {
public:
  virtual ~IMqttApplicationReceiver() = default;

  /** Publishes one status for the deployment identified by its transfer ID. */
  virtual void publishStatus(const ApplicationDeploymentStatus &deploymentStatus) = 0;
};

} // namespace messaging
} // namespace iot
