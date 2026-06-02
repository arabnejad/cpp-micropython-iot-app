#include "iot/messaging/application_message_queue.h"

#include <gtest/gtest.h>

#include <chrono>

namespace iot {
namespace messaging {
namespace {

TEST(ApplicationMessageQueueTest, ReturnsMessagesInTheOrderTheyWereAdded) {
  ApplicationMessageQueue applicationMessageQueue(2U);
  ASSERT_TRUE(applicationMessageQueue.tryPush({"first"}));
  ASSERT_TRUE(applicationMessageQueue.tryPush({"second"}));

  const auto firstReceivedMessage  = applicationMessageQueue.waitAndPopMessage(std::chrono::milliseconds(0));
  const auto secondReceivedMessage = applicationMessageQueue.waitAndPopMessage(std::chrono::milliseconds(0));

  ASSERT_TRUE(firstReceivedMessage.has_value());
  ASSERT_TRUE(secondReceivedMessage.has_value());
  EXPECT_EQ(firstReceivedMessage->payload, "first");
  EXPECT_EQ(secondReceivedMessage->payload, "second");
}

TEST(ApplicationMessageQueueTest, RefusesAMessageWhenItsFixedCapacityIsReached) {
  ApplicationMessageQueue applicationMessageQueue(1U);
  ASSERT_TRUE(applicationMessageQueue.tryPush({"first"}));

  EXPECT_FALSE(applicationMessageQueue.tryPush({"second"}));
}

TEST(ApplicationMessageQueueTest, ReturnsNoMessageWhenTheQueueStaysEmpty) {
  ApplicationMessageQueue applicationMessageQueue(1U);

  EXPECT_FALSE(applicationMessageQueue.waitAndPopMessage(std::chrono::milliseconds(0)).has_value());
}

TEST(ApplicationMessageQueueTest, RequiresANonZeroCapacity) {
  EXPECT_THROW(ApplicationMessageQueue(0U), std::invalid_argument);
}

} // namespace
} // namespace messaging
} // namespace iot
