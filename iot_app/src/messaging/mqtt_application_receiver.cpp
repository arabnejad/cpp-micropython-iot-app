#include "iot/messaging/mqtt_application_receiver.h"

#include <mosquitto.h>
#include <mqtt_protocol.h>

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
    : mqttReceiverSettings_(std::move(mqttReceiverSettings)), applicationMessageQueue_(applicationMessageQueue),
      mqttClientApi_(mqttClientApi),
      installTopic_("iot/devices/" + mqttReceiverSettings_.deviceId + "/applications/install") {
  if (mqttReceiverSettings_.deviceId.empty() || mqttReceiverSettings_.brokerHost.empty() ||
      mqttReceiverSettings_.brokerPort == 0U || mqttReceiverSettings_.keepAliveSeconds == 0U ||
      mqttReceiverSettings_.maximumMessageSizeInBytes == 0U) {
    IOT_LOG_ERROR(logger_, "Cannot create MQTT receiver; deviceId='", mqttReceiverSettings_.deviceId, "', brokerHost='",
                  mqttReceiverSettings_.brokerHost, "', brokerPort=", mqttReceiverSettings_.brokerPort,
                  ", keepAliveSeconds=", mqttReceiverSettings_.keepAliveSeconds,
                  ", maximumMessageSizeInBytes=", mqttReceiverSettings_.maximumMessageSizeInBytes);
    throw std::invalid_argument("MQTT application receiver settings are incomplete");
  }
}

MqttApplicationReceiver::~MqttApplicationReceiver() {
  stop();
}

void MqttApplicationReceiver::start() {
  if (mqttClient_ != nullptr) {
    return;
  }

  throwForMosquittoError(mqttClientApi_, mqttClientApi_.initializeLibrary(), "Could not initialize libmosquitto");
  libraryIsInitialized_      = true;
  const std::string clientId = "iot-app-" + mqttReceiverSettings_.deviceId;
  mqttClient_                = mqttClientApi_.createClient(clientId.c_str(), this);
  if (mqttClient_ == nullptr) {
    stop();
    throw std::runtime_error("Could not allocate the MQTT application receiver");
  }

  try {
    throwForMosquittoError(mqttClientApi_, mqttClientApi_.selectMqtt5(mqttClient_), "Could not select MQTT 5");
    throwForMosquittoError(mqttClientApi_, mqttClientApi_.setReconnectDelay(mqttClient_),
                           "Could not configure MQTT reconnection");
    if (!mqttReceiverSettings_.username.empty()) {
      const char *password = mqttReceiverSettings_.password.empty() ? nullptr : mqttReceiverSettings_.password.c_str();
      throwForMosquittoError(
          mqttClientApi_, mqttClientApi_.setCredentials(mqttClient_, mqttReceiverSettings_.username.c_str(), password),
          "Could not configure MQTT credentials");
    }

    mqttClientApi_.setConnectedCallback(mqttClient_, &MqttApplicationReceiver::handleConnected);
    mqttClientApi_.setDisconnectedCallback(mqttClient_, &MqttApplicationReceiver::handleDisconnected);
    mqttClientApi_.setMessageCallback(mqttClient_, &MqttApplicationReceiver::handleMessage);

    throwForMosquittoError(mqttClientApi_,
                           mqttClientApi_.connectAsync(mqttClient_, mqttReceiverSettings_.brokerHost.c_str(),
                                                       mqttReceiverSettings_.brokerPort,
                                                       mqttReceiverSettings_.keepAliveSeconds),
                           "Could not start the MQTT broker connection");
    throwForMosquittoError(mqttClientApi_, mqttClientApi_.startNetworkLoop(mqttClient_),
                           "Could not start the MQTT network thread");
    networkLoopIsRunning_ = true;
    IOT_LOG_INFO(logger_, "Connecting to MQTT broker ", mqttReceiverSettings_.brokerHost, ':',
                 mqttReceiverSettings_.brokerPort, " and subscribing to ", installTopic_);
  } catch (...) {
    stop();
    throw;
  }
}

void MqttApplicationReceiver::stop() noexcept {
  if (mqttClient_ != nullptr) {
    static_cast<void>(mqttClientApi_.disconnect(mqttClient_));
    if (networkLoopIsRunning_) {
      static_cast<void>(mqttClientApi_.stopNetworkLoop(mqttClient_));
    }
    networkLoopIsRunning_ = false;
    mqttClientApi_.destroyClient(mqttClient_);
    mqttClient_ = nullptr;
  }
  if (libraryIsInitialized_) {
    static_cast<void>(mqttClientApi_.cleanupLibrary());
    libraryIsInitialized_ = false;
  }
}

void MqttApplicationReceiver::publishStatus(const ApplicationDeploymentStatus &deploymentStatus) {
  if (mqttClient_ == nullptr || deploymentStatus.transferId.empty()) {
    IOT_LOG_DEBUG(logger_, "Skipping deployment status publish; mqttConnected=", mqttClient_ != nullptr,
                  ", transferIdEmpty=", deploymentStatus.transferId.empty());
    return;
  }

  try {
    const std::string statusPayload = ApplicationDeploymentMessageParser::serializeStatusPayload(deploymentStatus);
    if (statusPayload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        deploymentStatus.transferId.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
      IOT_LOG_ERROR(logger_, "Could not publish MQTT deployment status because it is too large");
      return;
    }

    mosquitto_property *mqttProperties      = nullptr;
    int                 mosquittoResultCode = mqttClientApi_.addJsonContentType(&mqttProperties);
    if (mosquittoResultCode == MOSQ_ERR_SUCCESS) {
      mosquittoResultCode =
          mqttClientApi_.addCorrelationData(&mqttProperties, deploymentStatus.transferId.data(),
                                            static_cast<std::uint16_t>(deploymentStatus.transferId.size()));
    }

    const std::string statusTopic =
        "iot/devices/" + mqttReceiverSettings_.deviceId + "/applications/status/" + deploymentStatus.transferId;
    if (mosquittoResultCode == MOSQ_ERR_SUCCESS) {
      mosquittoResultCode = mqttClientApi_.publish(mqttClient_, statusTopic.c_str(), statusPayload.data(),
                                                   static_cast<int>(statusPayload.size()), mqttProperties);
    }
    mqttClientApi_.freeProperties(&mqttProperties);
    if (mosquittoResultCode != MOSQ_ERR_SUCCESS) {
      IOT_LOG_ERROR(logger_,
                    "Could not publish MQTT deployment status: ", mqttClientApi_.errorText(mosquittoResultCode));
    } else {
      IOT_LOG_DEBUG(logger_, "Published deployment status; transferId=", deploymentStatus.transferId,
                    ", state=", deploymentStatus.deploymentState, ", payloadBytes=", statusPayload.size());
    }
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(logger_, "Could not create MQTT deployment status: ", error.what());
  }
}

const std::string &MqttApplicationReceiver::installTopic() const noexcept {
  return installTopic_;
}

void MqttApplicationReceiver::handleConnected(struct mosquitto *mqttClient, void *userData, int reasonCode, int,
                                              const mosquitto_property *) {
  auto *mqttApplicationReceiver = static_cast<MqttApplicationReceiver *>(userData);
  if (mqttApplicationReceiver == nullptr) {
    return;
  }
  if (reasonCode != MQTT_RC_SUCCESS) {
    IOT_LOG_ERROR(mqttApplicationReceiver->logger_, "MQTT broker rejected the IoT App connection: ",
                  mqttApplicationReceiver->mqttClientApi_.reasonText(reasonCode));
    return;
  }

  const int mosquittoResultCode =
      mqttApplicationReceiver->mqttClientApi_.subscribe(mqttClient, mqttApplicationReceiver->installTopic_.c_str());
  if (mosquittoResultCode == MOSQ_ERR_SUCCESS) {
    IOT_LOG_INFO(mqttApplicationReceiver->logger_, "Connected to the MQTT broker and subscribed for applications");
  } else {
    IOT_LOG_ERROR(mqttApplicationReceiver->logger_, "Could not subscribe for MQTT applications: ",
                  mqttApplicationReceiver->mqttClientApi_.errorText(mosquittoResultCode));
  }
}

void MqttApplicationReceiver::handleDisconnected(struct mosquitto *, void *userData, int reasonCode,
                                                 const mosquitto_property *) {
  auto *mqttApplicationReceiver = static_cast<MqttApplicationReceiver *>(userData);
  if (mqttApplicationReceiver != nullptr && reasonCode != MQTT_RC_SUCCESS) {
    IOT_LOG_WARNING(mqttApplicationReceiver->logger_, "MQTT receiver disconnected; libmosquitto will retry: ",
                    mqttApplicationReceiver->mqttClientApi_.reasonText(reasonCode));
  }
}

void MqttApplicationReceiver::handleMessage(struct mosquitto *, void *userData,
                                            const struct mosquitto_message *mqttMessage, const mosquitto_property *) {
  auto *mqttApplicationReceiver = static_cast<MqttApplicationReceiver *>(userData);
  if (mqttApplicationReceiver == nullptr || mqttMessage == nullptr || mqttMessage->topic == nullptr ||
      mqttApplicationReceiver->installTopic_ != mqttMessage->topic || mqttMessage->payloadlen <= 0 ||
      mqttMessage->payload == nullptr) {
    return;
  }
  if (static_cast<std::size_t>(mqttMessage->payloadlen) >
      mqttApplicationReceiver->mqttReceiverSettings_.maximumMessageSizeInBytes) {
    IOT_LOG_WARNING(mqttApplicationReceiver->logger_,
                    "Ignored an MQTT application message larger than the configured limit");
    return;
  }

  IOT_LOG_DEBUG(mqttApplicationReceiver->logger_, "Received MQTT application message; topic=", mqttMessage->topic,
                ", payloadBytes=", mqttMessage->payloadlen);

  const auto                *payloadBytes = static_cast<const char *>(mqttMessage->payload);
  ReceivedApplicationMessage receivedApplicationMessage{
      std::string(payloadBytes, payloadBytes + static_cast<std::size_t>(mqttMessage->payloadlen))};
  if (!mqttApplicationReceiver->applicationMessageQueue_.tryPush(std::move(receivedApplicationMessage))) {
    IOT_LOG_WARNING(mqttApplicationReceiver->logger_,
                    "Ignored an MQTT application message because the command queue is full");
  }
}

} // namespace messaging
} // namespace iot
