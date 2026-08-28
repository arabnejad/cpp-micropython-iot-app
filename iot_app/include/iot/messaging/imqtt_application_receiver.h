#pragma once

#include "iot/messaging/application_deployment_message.h"

namespace iot {
namespace messaging {

/*
 * Status-publishing part of MqttApplicationReceiver.
 *
 * The deployment controller only needs to publish replies. Connection,
 * subscription, and callback handling remain in MqttApplicationReceiver.
 */
class IMqttApplicationReceiver {
public:
  virtual ~IMqttApplicationReceiver() = default;

  /* Publishes one deployment status. */
  virtual void publishStatus(const ApplicationDeploymentStatus &deploymentStatus) = 0;
};

} // namespace messaging
} // namespace iot
