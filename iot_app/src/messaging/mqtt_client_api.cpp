#include "internal/imqtt_client_api.h"

#include <mosquitto.h>

/*
 * Buildroot currently provides Mosquitto 2.1.2, while the Yocto Scarthgap
 * recipe provides 2.0.22 by default. Mosquitto 2.0 keeps some MQTT constants
 * used below in mqtt_protocol.h, so the Yocto build needs that extra header.
 *
 * Starting with Mosquitto 2.1, mosquitto.h includes those definitions itself
 * and the old mqtt_protocol.h compatibility header prints a warning when it
 * is included directly. LIBMOSQUITTO_VERSION_NUMBER uses 2001000 for version
 * 2.1.0, so only older versions include the separate header. This also keeps
 * native Raspberry Pi OS builds working when they provide Mosquitto 2.0.
 */
#if LIBMOSQUITTO_VERSION_NUMBER < 2001000
#include <mqtt_protocol.h>
#endif

namespace iot {
namespace messaging {
namespace internal {
namespace {

class MqttClientApi final : public IMqttClientApi {
public:
  int initializeLibrary() override {
    return mosquitto_lib_init();
  }

  int cleanupLibrary() override {
    return mosquitto_lib_cleanup();
  }

  struct mosquitto *createClient(const char *clientId, void *userData) override {
    return mosquitto_new(clientId, true, userData);
  }

  void destroyClient(struct mosquitto *mqttClient) override {
    mosquitto_destroy(mqttClient);
  }

  int selectMqtt5(struct mosquitto *mqttClient) override {
    return mosquitto_int_option(mqttClient, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);
  }

  int setReconnectDelay(struct mosquitto *mqttClient) override {
    return mosquitto_reconnect_delay_set(mqttClient, 1U, 30U, true);
  }

  int setCredentials(struct mosquitto *mqttClient, const char *username, const char *password) override {
    return mosquitto_username_pw_set(mqttClient, username, password);
  }

  void setConnectedCallback(struct mosquitto *mqttClient, MqttConnectedCallback callback) override {
    mosquitto_connect_v5_callback_set(mqttClient, callback);
  }

  void setDisconnectedCallback(struct mosquitto *mqttClient, MqttDisconnectedCallback callback) override {
    mosquitto_disconnect_v5_callback_set(mqttClient, callback);
  }

  void setMessageCallback(struct mosquitto *mqttClient, MqttMessageCallback callback) override {
    mosquitto_message_v5_callback_set(mqttClient, callback);
  }

  int connectAsync(struct mosquitto *mqttClient, const char *host, int port, int keepAliveSeconds) override {
    return mosquitto_connect_async(mqttClient, host, port, keepAliveSeconds);
  }

  int startNetworkLoop(struct mosquitto *mqttClient) override {
    return mosquitto_loop_start(mqttClient);
  }

  int disconnect(struct mosquitto *mqttClient) override {
    return mosquitto_disconnect(mqttClient);
  }

  int stopNetworkLoop(struct mosquitto *mqttClient) override {
    return mosquitto_loop_stop(mqttClient, true);
  }

  int subscribe(struct mosquitto *mqttClient, const char *topic) override {
    return mosquitto_subscribe_v5(mqttClient, nullptr, topic, 1, 0, nullptr);
  }

  int addJsonContentType(mosquitto_property **mqttProperties) override {
    return mosquitto_property_add_string(mqttProperties, MQTT_PROP_CONTENT_TYPE, "application/json");
  }

  int publish(struct mosquitto *mqttClient, const char *topic, const void *payload, int payloadSize,
              const mosquitto_property *mqttProperties) override {
    return mosquitto_publish_v5(mqttClient, nullptr, topic, payloadSize, payload, 1, false, mqttProperties);
  }

  void freeProperties(mosquitto_property **mqttProperties) override {
    mosquitto_property_free_all(mqttProperties);
  }

  const char *errorText(int errorCode) const override {
    return mosquitto_strerror(errorCode);
  }

  const char *reasonText(int reasonCode) const override {
    return mosquitto_reason_string(reasonCode);
  }
};

} // namespace

IMqttClientApi &mqttClientApi() {
  static MqttClientApi api;
  return api;
}

} // namespace internal
} // namespace messaging
} // namespace iot
