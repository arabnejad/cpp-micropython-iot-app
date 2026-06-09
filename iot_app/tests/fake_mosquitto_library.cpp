#include "fake_mosquitto_library.h"

namespace iot {
namespace tests {

int FakeMqttClientApi::initializeLibrary() {
  return libraryInitializationResult;
}

int FakeMqttClientApi::cleanupLibrary() {
  libraryWasCleanedUp = true;
  return MOSQ_ERR_SUCCESS;
}

struct mosquitto *FakeMqttClientApi::createClient(const char *, void *userData) {
  if (failClientCreation) {
    return nullptr;
  }
  m_userData = userData;
  return m_client;
}

void FakeMqttClientApi::destroyClient(struct mosquitto *) {
  clientWasDestroyed = true;
}

int FakeMqttClientApi::selectMqtt5(struct mosquitto *) {
  return integerOptionResult;
}

int FakeMqttClientApi::setReconnectDelay(struct mosquitto *) {
  return reconnectDelayResult;
}

int FakeMqttClientApi::setCredentials(struct mosquitto *, const char *username, const char *password) {
  credentialsWereConfigured = true;
  configuredUsername        = username == nullptr ? "" : username;
  configuredPassword        = password == nullptr ? "" : password;
  return credentialsResult;
}

void FakeMqttClientApi::setConnectedCallback(struct mosquitto *, messaging::MqttConnectedCallback callback) {
  m_connectedCallback = callback;
}

void FakeMqttClientApi::setDisconnectedCallback(struct mosquitto *, messaging::MqttDisconnectedCallback callback) {
  m_disconnectedCallback = callback;
}

void FakeMqttClientApi::setMessageCallback(struct mosquitto *, messaging::MqttMessageCallback callback) {
  m_messageCallback = callback;
}

int FakeMqttClientApi::connectAsync(struct mosquitto *, const char *host, int port, int keepAliveSeconds) {
  connectedHost             = host == nullptr ? "" : host;
  connectedPort             = port;
  connectedKeepAliveSeconds = keepAliveSeconds;
  return asynchronousConnectionResult;
}

int FakeMqttClientApi::startNetworkLoop(struct mosquitto *) {
  return networkLoopStartResult;
}

int FakeMqttClientApi::disconnect(struct mosquitto *) {
  return MOSQ_ERR_SUCCESS;
}

int FakeMqttClientApi::stopNetworkLoop(struct mosquitto *) {
  networkLoopWasStopped = true;
  return MOSQ_ERR_SUCCESS;
}

int FakeMqttClientApi::subscribe(struct mosquitto *, const char *topic) {
  subscribedTopic = topic == nullptr ? "" : topic;
  return subscriptionResult;
}

int FakeMqttClientApi::addJsonContentType(mosquitto_property **mqttProperties) {
  if (contentTypePropertyResult == MOSQ_ERR_SUCCESS) {
    *mqttProperties = reinterpret_cast<mosquitto_property *>(this);
  }
  return contentTypePropertyResult;
}

int FakeMqttClientApi::publish(struct mosquitto *, const char *topic, const void *payload, int payloadSize,
                               const mosquitto_property *) {
  publishedTopic = topic == nullptr ? "" : topic;
  publishedPayload.assign(static_cast<const char *>(payload), static_cast<std::size_t>(payloadSize));
  publishedQualityOfService = 1;
  return publishResult;
}

void FakeMqttClientApi::freeProperties(mosquitto_property **mqttProperties) {
  ++propertyFreeCount;
  *mqttProperties = nullptr;
}

const char *FakeMqttClientApi::errorText(int) const {
  return "fake libmosquitto error";
}

const char *FakeMqttClientApi::reasonText(int) const {
  return "fake MQTT reason";
}

void FakeMqttClientApi::reportConnectionResult(int reasonCode) {
  if (m_connectedCallback != nullptr) {
    m_connectedCallback(m_client, m_userData, reasonCode, 0, nullptr);
  }
}

void FakeMqttClientApi::reportDisconnection(int reasonCode) {
  if (m_disconnectedCallback != nullptr) {
    m_disconnectedCallback(m_client, m_userData, reasonCode, nullptr);
  }
}

void FakeMqttClientApi::deliverMessage(const std::string &topic, const std::string &payload) {
  if (m_messageCallback == nullptr) {
    return;
  }
  struct mosquitto_message message {};
  message.topic      = const_cast<char *>(topic.c_str());
  message.payload    = const_cast<char *>(payload.data());
  message.payloadlen = static_cast<int>(payload.size());
  m_messageCallback(m_client, m_userData, &message, nullptr);
}

void FakeMqttClientApi::deliverEmptyMessage(const std::string &topic) {
  if (m_messageCallback == nullptr) {
    return;
  }
  struct mosquitto_message message {};
  message.topic = const_cast<char *>(topic.c_str());
  m_messageCallback(m_client, m_userData, &message, nullptr);
}

} // namespace tests
} // namespace iot
