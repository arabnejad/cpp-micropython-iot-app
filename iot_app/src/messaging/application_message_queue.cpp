#include "iot/messaging/application_message_queue.h"

#include <stdexcept>
#include <utility>

namespace iot {
namespace messaging {

ApplicationMessageQueue::ApplicationMessageQueue(std::size_t maximumQueuedMessages)
    : maximumQueuedMessages_(maximumQueuedMessages) {
  if (maximumQueuedMessages_ == 0U) {
    throw std::invalid_argument("Application message queue requires a non-zero capacity");
  }
}

bool ApplicationMessageQueue::tryPush(ReceivedApplicationMessage receivedApplicationMessage) {
  {
    const std::lock_guard<std::mutex> queueLock(queueMutex_);
    if (queuedMessages_.size() >= maximumQueuedMessages_) {
      return false;
    }
    queuedMessages_.push_back(std::move(receivedApplicationMessage));
  }
  messageAvailable_.notify_one();
  return true;
}

std::optional<ReceivedApplicationMessage>
ApplicationMessageQueue::waitAndPopMessage(std::chrono::milliseconds maximumWaitTime) {
  std::unique_lock<std::mutex> queueLock(queueMutex_);
  messageAvailable_.wait_for(queueLock, maximumWaitTime, [this] { return !queuedMessages_.empty(); });
  if (queuedMessages_.empty()) {
    return std::nullopt;
  }

  auto receivedApplicationMessage = std::move(queuedMessages_.front());
  queuedMessages_.pop_front();
  return receivedApplicationMessage;
}

} // namespace messaging
} // namespace iot
