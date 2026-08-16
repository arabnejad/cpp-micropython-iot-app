#include "iot/messaging/mqtt_application_receiver.h"

#include "fake_mosquitto_library.h"

#include <gtest/gtest.h>
#include <mqtt_protocol.h>

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
    receiver_ = std::make_unique<MqttApplicationReceiver>(std::move(settings), receivedMessages_, mqttClientApi_);
  }

  void expectStartupFailureWhen(int &operationResult) {
    operationResult = MOSQ_ERR_INVAL;
    createReceiver(settingsWithCredentials());
    EXPECT_THROW(receiver_->start(), std::runtime_error);
    receiver_.reset();
    operationResult = MOSQ_ERR_SUCCESS;
  }

  static MqttApplicationReceiverSettings settingsWithCredentials() {
    auto settings     = validMqttReceiverSettings();
    settings.username = "device-user";
    settings.password = "device-password";
    return settings;
  }

  tests::FakeMqttClientApi                 mqttClientApi_;
  ApplicationMessageQueue                  receivedMessages_{2U};
  std::unique_ptr<MqttApplicationReceiver> receiver_;
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

  receiver_->start();
  receiver_->start();

  EXPECT_EQ(receiver_->installTopic(), "iot/devices/test-device/applications/install");
  EXPECT_EQ(mqttClientApi_.connectedHost, "mqtt.example.test");
  EXPECT_EQ(mqttClientApi_.connectedPort, 1884);
  EXPECT_EQ(mqttClientApi_.connectedKeepAliveSeconds, 45);
  EXPECT_TRUE(mqttClientApi_.credentialsWereConfigured);
  EXPECT_EQ(mqttClientApi_.configuredUsername, "device-user");
  EXPECT_EQ(mqttClientApi_.configuredPassword, "device-password");

  receiver_->stop();
  receiver_->stop();
  EXPECT_TRUE(mqttClientApi_.networkLoopWasStopped);
  EXPECT_TRUE(mqttClientApi_.clientWasDestroyed);
  EXPECT_TRUE(mqttClientApi_.libraryWasCleanedUp);
}

TEST_F(MqttApplicationReceiverTest, SubscribesAfterAConnectionAndHandlesBrokerErrors) {
  createReceiver();
  receiver_->start();

  mqttClientApi_.reportConnectionResult(MQTT_RC_NOT_AUTHORIZED);
  EXPECT_TRUE(mqttClientApi_.subscribedTopic.empty());

  mqttClientApi_.subscriptionResult = MOSQ_ERR_INVAL;
  mqttClientApi_.reportConnectionResult(MQTT_RC_SUCCESS);
  EXPECT_EQ(mqttClientApi_.subscribedTopic, receiver_->installTopic());

  mqttClientApi_.subscriptionResult = MOSQ_ERR_SUCCESS;
  mqttClientApi_.reportConnectionResult(MQTT_RC_SUCCESS);
  mqttClientApi_.reportDisconnection(MQTT_RC_SESSION_TAKEN_OVER);
  mqttClientApi_.reportDisconnection(MQTT_RC_SUCCESS);
}

TEST_F(MqttApplicationReceiverTest, CopiesOnlyValidInstallMessagesIntoTheBoundedQueue) {
  createReceiver();
  receiver_->start();

  mqttClientApi_.deliverEmptyMessage(receiver_->installTopic());
  mqttClientApi_.deliverMessage("another/topic", "ignored");
  mqttClientApi_.deliverMessage(receiver_->installTopic(), std::string(33U, 'x'));
  EXPECT_FALSE(receivedMessages_.waitAndPopMessage(std::chrono::milliseconds(0)).has_value());

  mqttClientApi_.deliverMessage(receiver_->installTopic(), "first application");
  mqttClientApi_.deliverMessage(receiver_->installTopic(), "queue is already full");
  const auto receivedMessage = receivedMessages_.waitAndPopMessage(std::chrono::milliseconds(0));
  ASSERT_TRUE(receivedMessage.has_value());
  EXPECT_EQ(receivedMessage->payload, "first application");
}

TEST_F(MqttApplicationReceiverTest, PublishesJsonStatusToTheCalculatedStatusTopic) {
  createReceiver();

  receiver_->publishStatus({"before-start", "received", "app", "ignored"});
  EXPECT_TRUE(mqttClientApi_.publishedTopic.empty());

  receiver_->start();
  receiver_->publishStatus({"", "received", "app", "ignored"});
  receiver_->publishStatus({"transfer-42", "started", "app", "Application started"});

  EXPECT_EQ(mqttClientApi_.publishedTopic, "iot/devices/test-device/applications/status/transfer-42");
  EXPECT_NE(mqttClientApi_.publishedPayload.find("\"started\""), std::string::npos);
  EXPECT_EQ(mqttClientApi_.publishedQualityOfService, 1);
  EXPECT_EQ(mqttClientApi_.propertyFreeCount, 1U);
}

TEST_F(MqttApplicationReceiverTest, CleansPropertiesWhenStatusPublicationFails) {
  createReceiver();
  receiver_->start();

  mqttClientApi_.contentTypePropertyResult = MOSQ_ERR_NOMEM;
  receiver_->publishStatus({"property-failure", "failed", "app", "failure"});
  EXPECT_EQ(mqttClientApi_.propertyFreeCount, 1U);

  mqttClientApi_.contentTypePropertyResult     = MOSQ_ERR_SUCCESS;
  mqttClientApi_.correlationDataPropertyResult = MOSQ_ERR_NOMEM;
  receiver_->publishStatus({"correlation-failure", "failed", "app", "failure"});
  EXPECT_EQ(mqttClientApi_.propertyFreeCount, 2U);

  mqttClientApi_.correlationDataPropertyResult = MOSQ_ERR_SUCCESS;
  mqttClientApi_.publishResult                 = MOSQ_ERR_NO_CONN;
  receiver_->publishStatus({"publish-failure", "failed", "app", "failure"});
  EXPECT_EQ(mqttClientApi_.propertyFreeCount, 3U);
}

TEST_F(MqttApplicationReceiverTest, ReleasesResourcesAfterEveryStartupFailure) {
  expectStartupFailureWhen(mqttClientApi_.libraryInitializationResult);
  expectStartupFailureWhen(mqttClientApi_.integerOptionResult);
  expectStartupFailureWhen(mqttClientApi_.reconnectDelayResult);
  expectStartupFailureWhen(mqttClientApi_.credentialsResult);
  expectStartupFailureWhen(mqttClientApi_.asynchronousConnectionResult);
  expectStartupFailureWhen(mqttClientApi_.networkLoopStartResult);

  mqttClientApi_.failClientCreation = true;
  createReceiver();
  EXPECT_THROW(receiver_->start(), std::runtime_error);
  EXPECT_TRUE(mqttClientApi_.libraryWasCleanedUp);
}

} // namespace
} // namespace messaging
} // namespace iot
