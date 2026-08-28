#include "iot/messaging/mqtt_application_receiver.h"

#include <mosquitto.h>

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace iot {
namespace messaging {
namespace {

void throwForMosquittoError(const IMqttClientApi &mqttClientApi, int mosquittoResultCode, const char *operation) {
  if (mosquittoResultCode != MOSQ_ERR_SUCCESS) {
    throw std::runtime_error(std::string(operation) + ": " + mqttClientApi.errorText(mosquittoResultCode));
  }
}

} // namespace

MqttApplicationReceiver::MqttApplicationReceiver(MqttApplicationReceiverSettings mqttReceiverSettings,
                                                 ApplicationMessageQueue        &applicationMessageQueue,
                                                 IMqttClientApi                 &mqttClientApi)
    : m_mqttReceiverSettings(std::move(mqttReceiverSettings)), m_applicationMessageQueue(applicationMessageQueue),
      m_mqttClientApi(mqttClientApi),
      m_installTopic("iot/devices/" + m_mqttReceiverSettings.deviceId + "/applications/install") {
  if (m_mqttReceiverSettings.deviceId.empty() || m_mqttReceiverSettings.brokerHost.empty() ||
      m_mqttReceiverSettings.brokerPort == 0U || m_mqttReceiverSettings.keepAliveSeconds == 0U ||
      m_mqttReceiverSettings.maximumMessageSizeInBytes == 0U) {
    IOT_LOG_ERROR(m_logger, "Cannot create MQTT receiver; deviceId='", m_mqttReceiverSettings.deviceId,
                  "', brokerHost='", m_mqttReceiverSettings.brokerHost,
                  "', brokerPort=", m_mqttReceiverSettings.brokerPort,
                  ", keepAliveSeconds=", m_mqttReceiverSettings.keepAliveSeconds,
                  ", maximumMessageSizeInBytes=", m_mqttReceiverSettings.maximumMessageSizeInBytes);
    throw std::invalid_argument("MQTT application receiver settings are incomplete");
  }
}

MqttApplicationReceiver::~MqttApplicationReceiver() {
  stop();
}

void MqttApplicationReceiver::start() {
  if (m_mqttClient != nullptr) {
    return;
  }

  throwForMosquittoError(m_mqttClientApi, m_mqttClientApi.initializeLibrary(), "Could not initialize libmosquitto");
  m_libraryIsInitialized     = true;
  const std::string clientId = "iot-app-" + m_mqttReceiverSettings.deviceId;
  m_mqttClient               = m_mqttClientApi.createClient(clientId.c_str(), this);
  if (m_mqttClient == nullptr) {
    stop();
    throw std::runtime_error("Could not allocate the MQTT application receiver");
  }

  try {
    throwForMosquittoError(m_mqttClientApi, m_mqttClientApi.selectMqtt5(m_mqttClient), "Could not select MQTT 5");
    throwForMosquittoError(m_mqttClientApi, m_mqttClientApi.setReconnectDelay(m_mqttClient),
                           "Could not configure MQTT reconnection");
    if (!m_mqttReceiverSettings.username.empty()) {
      const char *password =
          m_mqttReceiverSettings.password.empty() ? nullptr : m_mqttReceiverSettings.password.c_str();
      throwForMosquittoError(
          m_mqttClientApi,
          m_mqttClientApi.setCredentials(m_mqttClient, m_mqttReceiverSettings.username.c_str(), password),
          "Could not configure MQTT credentials");
    }

    m_mqttClientApi.setConnectedCallback(m_mqttClient, &MqttApplicationReceiver::handleConnected);
    m_mqttClientApi.setDisconnectedCallback(m_mqttClient, &MqttApplicationReceiver::handleDisconnected);
    m_mqttClientApi.setMessageCallback(m_mqttClient, &MqttApplicationReceiver::handleMessage);

    throwForMosquittoError(m_mqttClientApi,
                           m_mqttClientApi.connectAsync(m_mqttClient, m_mqttReceiverSettings.brokerHost.c_str(),
                                                        m_mqttReceiverSettings.brokerPort,
                                                        m_mqttReceiverSettings.keepAliveSeconds),
                           "Could not start the MQTT broker connection");
    throwForMosquittoError(m_mqttClientApi, m_mqttClientApi.startNetworkLoop(m_mqttClient),
                           "Could not start the MQTT network thread");
    m_networkLoopIsRunning = true;
    IOT_LOG_INFO(m_logger, "Connecting to MQTT broker ", m_mqttReceiverSettings.brokerHost, ':',
                 m_mqttReceiverSettings.brokerPort, " and subscribing to ", m_installTopic);
  } catch (...) {
    stop();
    throw;
  }
}

void MqttApplicationReceiver::stop() noexcept {
  if (m_mqttClient != nullptr) {
    static_cast<void>(m_mqttClientApi.disconnect(m_mqttClient));
    if (m_networkLoopIsRunning) {
      static_cast<void>(m_mqttClientApi.stopNetworkLoop(m_mqttClient));
    }
    m_networkLoopIsRunning = false;
    m_mqttClientApi.destroyClient(m_mqttClient);
    m_mqttClient = nullptr;
  }
  if (m_libraryIsInitialized) {
    static_cast<void>(m_mqttClientApi.cleanupLibrary());
    m_libraryIsInitialized = false;
  }
}

void MqttApplicationReceiver::publishStatus(const ApplicationDeploymentStatus &deploymentStatus) {
  if (m_mqttClient == nullptr || deploymentStatus.transferId.empty()) {
    IOT_LOG_DEBUG(m_logger, "Skipping deployment status publish; mqttConnected=", m_mqttClient != nullptr,
                  ", transferIdEmpty=", deploymentStatus.transferId.empty());
    return;
  }

  try {
    const std::string statusPayload = ApplicationDeploymentMessageParser::serializeStatusPayload(deploymentStatus);
    if (statusPayload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      IOT_LOG_ERROR(m_logger, "Could not publish MQTT deployment status because it is too large");
      return;
    }

    mosquitto_property *mqttProperties      = nullptr;
    int                 mosquittoResultCode = m_mqttClientApi.addJsonContentType(&mqttProperties);

    const std::string statusTopic =
        "iot/devices/" + m_mqttReceiverSettings.deviceId + "/applications/status/" + deploymentStatus.transferId;
    if (mosquittoResultCode == MOSQ_ERR_SUCCESS) {
      mosquittoResultCode = m_mqttClientApi.publish(m_mqttClient, statusTopic.c_str(), statusPayload.data(),
                                                    static_cast<int>(statusPayload.size()), mqttProperties);
    }
    m_mqttClientApi.freeProperties(&mqttProperties);
    if (mosquittoResultCode != MOSQ_ERR_SUCCESS) {
      IOT_LOG_ERROR(m_logger,
                    "Could not publish MQTT deployment status: ", m_mqttClientApi.errorText(mosquittoResultCode));
    } else {
      IOT_LOG_DEBUG(m_logger, "Published deployment status; transferId=", deploymentStatus.transferId,
                    ", state=", deploymentStatus.deploymentState, ", payloadBytes=", statusPayload.size());
    }
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(m_logger, "Could not create MQTT deployment status: ", error.what());
  }
}

const std::string &MqttApplicationReceiver::installTopic() const noexcept {
  return m_installTopic;
}

void MqttApplicationReceiver::handleConnected(struct mosquitto *mqttClient, void *userData, int reasonCode, int,
                                              const mosquitto_property *) {
  auto *mqttApplicationReceiver = static_cast<MqttApplicationReceiver *>(userData);
  if (mqttApplicationReceiver == nullptr) {
    return;
  }
  if (reasonCode != MQTT_RC_SUCCESS) {
    IOT_LOG_ERROR(mqttApplicationReceiver->m_logger, "MQTT broker rejected the IoT App connection: ",
                  mqttApplicationReceiver->m_mqttClientApi.reasonText(reasonCode));
    return;
  }

  const int mosquittoResultCode =
      mqttApplicationReceiver->m_mqttClientApi.subscribe(mqttClient, mqttApplicationReceiver->m_installTopic.c_str());
  if (mosquittoResultCode == MOSQ_ERR_SUCCESS) {
    IOT_LOG_INFO(mqttApplicationReceiver->m_logger, "Connected to the MQTT broker and subscribed for applications");
  } else {
    IOT_LOG_ERROR(mqttApplicationReceiver->m_logger, "Could not subscribe for MQTT applications: ",
                  mqttApplicationReceiver->m_mqttClientApi.errorText(mosquittoResultCode));
  }
}

void MqttApplicationReceiver::handleDisconnected(struct mosquitto *, void *userData, int reasonCode,
                                                 const mosquitto_property *) {
  auto *mqttApplicationReceiver = static_cast<MqttApplicationReceiver *>(userData);
  if (mqttApplicationReceiver != nullptr && reasonCode != MQTT_RC_SUCCESS) {
    IOT_LOG_WARNING(mqttApplicationReceiver->m_logger, "MQTT receiver disconnected; libmosquitto will retry: ",
                    mqttApplicationReceiver->m_mqttClientApi.reasonText(reasonCode));
  }
}

void MqttApplicationReceiver::handleMessage(struct mosquitto *, void *userData,
                                            const struct mosquitto_message *mqttMessage, const mosquitto_property *) {
  auto *mqttApplicationReceiver = static_cast<MqttApplicationReceiver *>(userData);
  if (mqttApplicationReceiver == nullptr || mqttMessage == nullptr || mqttMessage->topic == nullptr ||
      mqttApplicationReceiver->m_installTopic != mqttMessage->topic || mqttMessage->payloadlen <= 0 ||
      mqttMessage->payload == nullptr) {
    return;
  }
  if (static_cast<std::size_t>(mqttMessage->payloadlen) >
      mqttApplicationReceiver->m_mqttReceiverSettings.maximumMessageSizeInBytes) {
    IOT_LOG_WARNING(mqttApplicationReceiver->m_logger,
                    "Ignored an MQTT application message larger than the configured limit");
    return;
  }

  IOT_LOG_DEBUG(mqttApplicationReceiver->m_logger, "Received MQTT application message; topic=", mqttMessage->topic,
                ", payloadBytes=", mqttMessage->payloadlen);

  const auto                *payloadBytes = static_cast<const char *>(mqttMessage->payload);
  ReceivedApplicationMessage receivedApplicationMessage{
      std::string(payloadBytes, payloadBytes + static_cast<std::size_t>(mqttMessage->payloadlen))};
  if (!mqttApplicationReceiver->m_applicationMessageQueue.tryPush(std::move(receivedApplicationMessage))) {
    IOT_LOG_WARNING(mqttApplicationReceiver->m_logger,
                    "Ignored an MQTT application message because the command queue is full");
  }
}

} // namespace messaging
} // namespace iot
