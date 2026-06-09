#include "iot/messaging/mqtt_application_receiver.h"

#include "fake_mosquitto_library.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>

namespace iot {
namespace messaging {
namespace {

MqttApplicationReceiverSettings validMqttReceiverSettings() {
  MqttApplicationReceiverSettings settings;
  settings.deviceId                  = "test-device";
  settings.brokerHost                = "mqtt.example.test";
  settings.brokerPort                = 1884U;
  settings.keepAliveSeconds          = 45U;
  settings.maximumMessageSizeInBytes = 32U;
  return settings;
}

class MqttApplicationReceiverTest : public ::testing::Test {
protected:
  void createReceiver(MqttApplicationReceiverSettings settings = validMqttReceiverSettings()) {
    m_receiver = std::make_unique<MqttApplicationReceiver>(std::move(settings), m_receivedMessages, m_mqttClientApi);
  }

  void expectStartupFailureWhen(int &operationResult) {
    operationResult = MOSQ_ERR_INVAL;
    createReceiver(settingsWithCredentials());
    EXPECT_THROW(m_receiver->start(), std::runtime_error);
    m_receiver.reset();
    operationResult = MOSQ_ERR_SUCCESS;
  }

  static MqttApplicationReceiverSettings settingsWithCredentials() {
    auto settings     = validMqttReceiverSettings();
    settings.username = "device-user";
    settings.password = "device-password";
    return settings;
  }

  tests::FakeMqttClientApi                 m_mqttClientApi;
  ApplicationMessageQueue                  m_receivedMessages{2U};
  std::unique_ptr<MqttApplicationReceiver> m_receiver;
};

TEST(MqttApplicationReceiverSettingsTest, RejectsEachRequiredSettingWhenItIsMissing) {
  ApplicationMessageQueue                               receivedMessages(2U);
  tests::FakeMqttClientApi                              mqttClientApi;
  const std::array<MqttApplicationReceiverSettings, 5U> invalidSettings = [] {
    std::array<MqttApplicationReceiverSettings, 5U> settingsWithOneRequiredValueMissing;
    settingsWithOneRequiredValueMissing.fill(validMqttReceiverSettings());
    settingsWithOneRequiredValueMissing[0].deviceId.clear();
    settingsWithOneRequiredValueMissing[1].brokerHost.clear();
    settingsWithOneRequiredValueMissing[2].brokerPort                = 0U;
    settingsWithOneRequiredValueMissing[3].keepAliveSeconds          = 0U;
    settingsWithOneRequiredValueMissing[4].maximumMessageSizeInBytes = 0U;
    return settingsWithOneRequiredValueMissing;
  }();

  for (const auto &settings : invalidSettings) {
    EXPECT_THROW(MqttApplicationReceiver(settings, receivedMessages, mqttClientApi), std::invalid_argument);
  }
}

TEST_F(MqttApplicationReceiverTest, StartsWithTheConfiguredBrokerAndStopsItsResources) {
  createReceiver(settingsWithCredentials());

  m_receiver->start();
  m_receiver->start();

  EXPECT_EQ(m_receiver->installTopic(), "iot/devices/test-device/applications/install");
  EXPECT_EQ(m_mqttClientApi.connectedHost, "mqtt.example.test");
  EXPECT_EQ(m_mqttClientApi.connectedPort, 1884);
  EXPECT_EQ(m_mqttClientApi.connectedKeepAliveSeconds, 45);
  EXPECT_TRUE(m_mqttClientApi.credentialsWereConfigured);
  EXPECT_EQ(m_mqttClientApi.configuredUsername, "device-user");
  EXPECT_EQ(m_mqttClientApi.configuredPassword, "device-password");

  m_receiver->stop();
  m_receiver->stop();
  EXPECT_TRUE(m_mqttClientApi.networkLoopWasStopped);
  EXPECT_TRUE(m_mqttClientApi.clientWasDestroyed);
  EXPECT_TRUE(m_mqttClientApi.libraryWasCleanedUp);
}

TEST_F(MqttApplicationReceiverTest, SubscribesAfterAConnectionAndHandlesBrokerErrors) {
  createReceiver();
  m_receiver->start();

  m_mqttClientApi.reportConnectionResult(MQTT_RC_NOT_AUTHORIZED);
  EXPECT_TRUE(m_mqttClientApi.subscribedTopic.empty());

  m_mqttClientApi.subscriptionResult = MOSQ_ERR_INVAL;
  m_mqttClientApi.reportConnectionResult(MQTT_RC_SUCCESS);
  EXPECT_EQ(m_mqttClientApi.subscribedTopic, m_receiver->installTopic());

  m_mqttClientApi.subscriptionResult = MOSQ_ERR_SUCCESS;
  m_mqttClientApi.reportConnectionResult(MQTT_RC_SUCCESS);
  m_mqttClientApi.reportDisconnection(MQTT_RC_SESSION_TAKEN_OVER);
  m_mqttClientApi.reportDisconnection(MQTT_RC_SUCCESS);
}

TEST_F(MqttApplicationReceiverTest, CopiesOnlyValidInstallMessagesIntoTheBoundedQueue) {
  createReceiver();
  m_receiver->start();

  m_mqttClientApi.deliverEmptyMessage(m_receiver->installTopic());
  m_mqttClientApi.deliverMessage("another/topic", "ignored");
  m_mqttClientApi.deliverMessage(m_receiver->installTopic(), std::string(33U, 'x'));
  EXPECT_FALSE(m_receivedMessages.waitAndPopMessage(std::chrono::milliseconds(0)).has_value());

  m_mqttClientApi.deliverMessage(m_receiver->installTopic(), "first application");
  m_mqttClientApi.deliverMessage(m_receiver->installTopic(), "queue is already full");
  const auto receivedMessage = m_receivedMessages.waitAndPopMessage(std::chrono::milliseconds(0));
  ASSERT_TRUE(receivedMessage.has_value());
  EXPECT_EQ(receivedMessage->payload, "first application");
}

TEST_F(MqttApplicationReceiverTest, PublishesJsonStatusToTheCalculatedStatusTopic) {
  createReceiver();

  m_receiver->publishStatus({"before-start", "received", "app", "ignored"});
  EXPECT_TRUE(m_mqttClientApi.publishedTopic.empty());

  m_receiver->start();
  m_receiver->publishStatus({"", "received", "app", "ignored"});
  m_receiver->publishStatus({"transfer-42", "started", "app", "Application started"});

  EXPECT_EQ(m_mqttClientApi.publishedTopic, "iot/devices/test-device/applications/status/transfer-42");
  EXPECT_NE(m_mqttClientApi.publishedPayload.find("\"started\""), std::string::npos);
  EXPECT_EQ(m_mqttClientApi.publishedQualityOfService, 1);
  EXPECT_EQ(m_mqttClientApi.propertyFreeCount, 1U);
}

TEST_F(MqttApplicationReceiverTest, CleansPropertiesWhenStatusPublicationFails) {
  createReceiver();
  m_receiver->start();

  m_mqttClientApi.contentTypePropertyResult = MOSQ_ERR_NOMEM;
  m_receiver->publishStatus({"property-failure", "failed", "app", "failure"});
  EXPECT_EQ(m_mqttClientApi.propertyFreeCount, 1U);

  m_mqttClientApi.contentTypePropertyResult = MOSQ_ERR_SUCCESS;
  m_mqttClientApi.publishResult             = MOSQ_ERR_NO_CONN;
  m_receiver->publishStatus({"publish-failure", "failed", "app", "failure"});
  EXPECT_EQ(m_mqttClientApi.propertyFreeCount, 2U);
}

TEST_F(MqttApplicationReceiverTest, ReleasesResourcesAfterEveryStartupFailure) {
  expectStartupFailureWhen(m_mqttClientApi.libraryInitializationResult);
  expectStartupFailureWhen(m_mqttClientApi.integerOptionResult);
  expectStartupFailureWhen(m_mqttClientApi.reconnectDelayResult);
  expectStartupFailureWhen(m_mqttClientApi.credentialsResult);
  expectStartupFailureWhen(m_mqttClientApi.asynchronousConnectionResult);
  expectStartupFailureWhen(m_mqttClientApi.networkLoopStartResult);

  m_mqttClientApi.failClientCreation = true;
  createReceiver();
  EXPECT_THROW(m_receiver->start(), std::runtime_error);
  EXPECT_TRUE(m_mqttClientApi.libraryWasCleanedUp);
}

} // namespace
} // namespace messaging
} // namespace iot
