#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_message.h"
#include "iot/messaging/application_message_queue.h"
#include "iot/messaging/imqtt_application_receiver.h"
#include "iot/messaging/imqtt_client_api.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Only pointers to these libmosquitto types appear in this header, so forward
// declarations are enough here. The .cpp file includes mosquitto.h and
// mqtt_protocol.h. This avoids pulling those headers into every user of this
// class.
struct mosquitto;
struct mosquitto_message;
struct mqtt5__property;
typedef struct mqtt5__property mosquitto_property;

namespace iot {
namespace messaging {

/* MQTT connection settings for this Raspberry Pi. */
struct MqttApplicationReceiverSettings {
  std::string   deviceId;
  std::string   brokerHost;
  std::uint16_t brokerPort{1883};
  std::uint16_t keepAliveSeconds{60};
  std::string   username;
  std::string   password;
  std::size_t   maximumMessageSizeInBytes{1024U * 1024U};
};

/*
 * Receives application install messages and sends deployment status replies.
 *
 * Libmosquitto calls the handlers on its network thread. They only copy
 * size-limited messages into ApplicationMessageQueue. Validation, file writes,
 * and application switching stay on the main thread.
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

  /* Connects to the broker and subscribes to this device's install topic. */
  void start();

  /* Stops the MQTT network loop. Calling this more than once is safe. */
  void stop() noexcept;

  /* Publishes a deployment status on its transfer-specific status topic. */
  void publishStatus(const ApplicationDeploymentStatus &deploymentStatus) override;

  const std::string &installTopic() const noexcept;

private:
  static void handleConnected(struct mosquitto *mqttClient, void *userData, int reasonCode, int flags,
                              const mosquitto_property *mqttProperties);
  static void handleDisconnected(struct mosquitto *mqttClient, void *userData, int reasonCode,
                                 const mosquitto_property *mqttProperties);
  static void handleMessage(struct mosquitto *mqttClient, void *userData, const struct mosquitto_message *mqttMessage,
                            const mosquitto_property *mqttProperties);

  logging::Logger                 m_logger{"MqttApplicationReceiver"};
  MqttApplicationReceiverSettings m_mqttReceiverSettings;
  ApplicationMessageQueue        &m_applicationMessageQueue;
  IMqttClientApi                 &m_mqttClientApi;
  std::string                     m_installTopic;
  struct mosquitto               *m_mqttClient{nullptr};
  bool                            m_libraryIsInitialized{false};
  bool                            m_networkLoopIsRunning{false};
};

} // namespace messaging
} // namespace iot
