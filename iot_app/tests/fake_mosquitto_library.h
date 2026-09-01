#pragma once

#include "messaging/internal/imqtt_client_api.h"

#include <mosquitto.h>

#include <cstddef>
#include <string>

namespace iot {
namespace tests {

/*
 * In-memory MQTT API used by MqttApplicationReceiver tests.
 *
 * The receiver calls this object instead of opening a real broker connection.
 * Tests can choose the result of each operation and inspect what was sent.
 */
class FakeMqttClientApi final : public messaging::internal::IMqttClientApi {
public:
  int  libraryInitializationResult{MOSQ_ERR_SUCCESS};
  int  integerOptionResult{MOSQ_ERR_SUCCESS};
  int  reconnectDelayResult{MOSQ_ERR_SUCCESS};
  int  credentialsResult{MOSQ_ERR_SUCCESS};
  int  asynchronousConnectionResult{MOSQ_ERR_SUCCESS};
  int  networkLoopStartResult{MOSQ_ERR_SUCCESS};
  int  subscriptionResult{MOSQ_ERR_SUCCESS};
  int  contentTypePropertyResult{MOSQ_ERR_SUCCESS};
  int  publishResult{MOSQ_ERR_SUCCESS};
  bool failClientCreation{false};

  bool        libraryWasCleanedUp{false};
  bool        clientWasDestroyed{false};
  bool        networkLoopWasStopped{false};
  bool        credentialsWereConfigured{false};
  std::string configuredUsername;
  std::string configuredPassword;
  std::string connectedHost;
  int         connectedPort{0};
  int         connectedKeepAliveSeconds{0};
  std::string subscribedTopic;
  std::string publishedTopic;
  std::string publishedPayload;
  int         publishedQualityOfService{0};
  std::size_t propertyFreeCount{0U};

  int               initializeLibrary() override;
  int               cleanupLibrary() override;
  struct mosquitto *createClient(const char *clientId, void *userData) override;
  void              destroyClient(struct mosquitto *mqttClient) override;
  int               selectMqtt5(struct mosquitto *mqttClient) override;
  int               setReconnectDelay(struct mosquitto *mqttClient) override;
  int               setCredentials(struct mosquitto *mqttClient, const char *username, const char *password) override;
  void setConnectedCallback(struct mosquitto *mqttClient, messaging::internal::MqttConnectedCallback callback) override;
  void setDisconnectedCallback(struct mosquitto                             *mqttClient,
                               messaging::internal::MqttDisconnectedCallback callback) override;
  void setMessageCallback(struct mosquitto *mqttClient, messaging::internal::MqttMessageCallback callback) override;
  int  connectAsync(struct mosquitto *mqttClient, const char *host, int port, int keepAliveSeconds) override;
  int  startNetworkLoop(struct mosquitto *mqttClient) override;
  int  disconnect(struct mosquitto *mqttClient) override;
  int  stopNetworkLoop(struct mosquitto *mqttClient) override;
  int  subscribe(struct mosquitto *mqttClient, const char *topic) override;
  int  addJsonContentType(mosquitto_property **mqttProperties) override;
  int  publish(struct mosquitto *mqttClient, const char *topic, const void *payload, int payloadSize,
               const mosquitto_property *mqttProperties) override;
  void freeProperties(mosquitto_property **mqttProperties) override;
  const char *errorText(int errorCode) const override;
  const char *reasonText(int reasonCode) const override;

  void reportConnectionResult(int reasonCode);
  void reportDisconnection(int reasonCode);
  void deliverMessage(const std::string &topic, const std::string &payload);
  void deliverEmptyMessage(const std::string &topic);

private:
  struct mosquitto                             *m_client{reinterpret_cast<struct mosquitto *>(this)};
  void                                         *m_userData{nullptr};
  messaging::internal::MqttConnectedCallback    m_connectedCallback{nullptr};
  messaging::internal::MqttDisconnectedCallback m_disconnectedCallback{nullptr};
  messaging::internal::MqttMessageCallback      m_messageCallback{nullptr};
};

} // namespace tests
} // namespace iot
