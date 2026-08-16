#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_message.h"
#include "iot/messaging/application_message_queue.h"
#include "iot/messaging/imqtt_application_receiver.h"
#include "iot/messaging/imqtt_client_api.h"

#include <cstddef>
#include <cstdint>
#include <string>

// These types belong to libmosquitto. This header only uses pointers to them,
// so their names can be declared here without their complete definitions. The
// .cpp file includes <mosquitto.h> for the types and library functions, and
// <mqtt_protocol.h> for the MQTT 5 constants. Keeping those third-party
// includes in the .cpp file avoids adding them to every project file that
// includes this header.
struct mosquitto;
struct mosquitto_message;
struct mqtt5__property;
typedef struct mqtt5__property mosquitto_property;

namespace iot {
namespace messaging {

/** MQTT connection settings for this Raspberry Pi. */
struct MqttApplicationReceiverSettings {
  std::string   deviceId;
  std::string   brokerHost;
  std::uint16_t brokerPort{1883};
  std::uint16_t keepAliveSeconds{60};
  std::string   username;
  std::string   password;
  std::size_t   maximumMessageSizeInBytes{1024U * 1024U};
};

/**
 * Receives application install messages and sends deployment status replies.
 *
 * Libmosquitto runs its callbacks on a network thread. Those callbacks only
 * copy size-limited messages into `ApplicationMessageQueue`. The main thread
 * validates the message, writes files, and switches the Python application.
 */
class MqttApplicationReceiver : public IMqttApplicationReceiver {
public:
  MqttApplicationReceiver(MqttApplicationReceiverSettings mqttReceiverSettings,
                          ApplicationMessageQueue &applicationMessageQueue, IMqttClientApi &mqttClientApi);
  ~MqttApplicationReceiver();

  // Owns one MQTT client and network loop; copying and moving are disabled.
  MqttApplicationReceiver(const MqttApplicationReceiver &)            = delete;
  MqttApplicationReceiver &operator=(const MqttApplicationReceiver &) = delete;
  MqttApplicationReceiver(MqttApplicationReceiver &&)                 = delete;
  MqttApplicationReceiver &operator=(MqttApplicationReceiver &&)      = delete;

  /** Connects to the broker and subscribes to this device's install topic. */
  void start();

  /** Stops the MQTT network loop. Calling this more than once is safe. */
  void stop() noexcept;

  /** Publishes one deployment status to its transfer-specific status topic. */
  void publishStatus(const ApplicationDeploymentStatus &deploymentStatus) override;

  const std::string &installTopic() const noexcept;

private:
  /** Handles a broker connection result on libmosquitto's network thread. */
  static void handleConnected(struct mosquitto *mqttClient, void *userData, int reasonCode, int flags,
                              const mosquitto_property *mqttProperties);
  /** Handles an unexpected broker disconnection on the network thread. */
  static void handleDisconnected(struct mosquitto *mqttClient, void *userData, int reasonCode,
                                 const mosquitto_property *mqttProperties);
  /** Copies a received application message into the bounded main-thread queue. */
  static void handleMessage(struct mosquitto *mqttClient, void *userData, const struct mosquitto_message *mqttMessage,
                            const mosquitto_property *mqttProperties);

  logging::Logger                 logger_{"MqttApplicationReceiver"};
  MqttApplicationReceiverSettings mqttReceiverSettings_;
  ApplicationMessageQueue        &applicationMessageQueue_;
  IMqttClientApi                 &mqttClientApi_;
  std::string                     installTopic_;
  struct mosquitto               *mqttClient_{nullptr};
  bool                            libraryIsInitialized_{false};
  bool                            networkLoopIsRunning_{false};
};

} // namespace messaging
} // namespace iot
