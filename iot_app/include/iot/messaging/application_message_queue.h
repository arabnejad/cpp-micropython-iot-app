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

/** Copy of one MQTT application message waiting for the main thread. */
struct ReceivedApplicationMessage {
  std::string payload;
};

/**
 * Passes MQTT messages from libmosquitto's network thread to the main thread.
 */
class ApplicationMessageQueue {
public:
  explicit ApplicationMessageQueue(std::size_t maximumQueuedMessages);

  // Shared with MQTT callbacks; copying and moving are disabled.
  ApplicationMessageQueue(const ApplicationMessageQueue &)            = delete;
  ApplicationMessageQueue &operator=(const ApplicationMessageQueue &) = delete;
  ApplicationMessageQueue(ApplicationMessageQueue &&)                 = delete;
  ApplicationMessageQueue &operator=(ApplicationMessageQueue &&)      = delete;

  /**
   * Tries to add a message without waiting for space in the queue.
   *
   * A full queue returns false. This fixed limit stops a fast sender from using
   * all device memory while the main thread is switching applications.
   */
  bool tryPush(ReceivedApplicationMessage receivedApplicationMessage);

  /** Waits for a message, removes it from the queue, or returns empty on timeout. */
  std::optional<ReceivedApplicationMessage> waitAndPopMessage(std::chrono::milliseconds maximumWaitTime);

private:
  std::size_t                            maximumQueuedMessages_{0};
  std::mutex                             queueMutex_;
  std::condition_variable                messageAvailable_;
  std::deque<ReceivedApplicationMessage> queuedMessages_;
};

} // namespace messaging
} // namespace iot
