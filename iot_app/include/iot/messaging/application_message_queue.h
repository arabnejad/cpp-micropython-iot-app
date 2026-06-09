#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace iot {
namespace messaging {

/* MQTT payload waiting for the main thread. */
struct ReceivedApplicationMessage {
  std::string payload;
};

/*
 * Passes application messages from the MQTT thread to the main thread.
 */
class ApplicationMessageQueue {
public:
  explicit ApplicationMessageQueue(std::size_t maximumQueuedMessages);

  // Shared with MQTT callbacks; copying and moving are disabled.
  ApplicationMessageQueue(const ApplicationMessageQueue &)            = delete;
  ApplicationMessageQueue &operator=(const ApplicationMessageQueue &) = delete;
  ApplicationMessageQueue(ApplicationMessageQueue &&)                 = delete;
  ApplicationMessageQueue &operator=(ApplicationMessageQueue &&)      = delete;

  /*
   * Adds a message if space is available.
   *
   * The call returns false when the queue is full. The limit prevents a sender
   * from filling device memory while the main thread changes applications.
   */
  bool tryPush(ReceivedApplicationMessage receivedApplicationMessage);

  /* Waits for one message and removes it, or returns empty after the timeout. */
  std::optional<ReceivedApplicationMessage> waitAndPopMessage(std::chrono::milliseconds maximumWaitTime);

private:
  std::size_t                            m_maximumQueuedMessages{0};
  std::mutex                             m_queueMutex;
  std::condition_variable                m_messageAvailable;
  std::deque<ReceivedApplicationMessage> m_queuedMessages;
};

} // namespace messaging
} // namespace iot
