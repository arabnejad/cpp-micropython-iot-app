#include "iot/messaging/application_message_queue.h"

#include <stdexcept>
#include <utility>

namespace iot {
namespace messaging {

ApplicationMessageQueue::ApplicationMessageQueue(std::size_t maximumQueuedMessages)
    : m_maximumQueuedMessages(maximumQueuedMessages) {
  if (m_maximumQueuedMessages == 0U) {
    throw std::invalid_argument("Application message queue requires a non-zero capacity");
  }
}

bool ApplicationMessageQueue::tryPush(ReceivedApplicationMessage receivedApplicationMessage) {
  {
    const std::lock_guard<std::mutex> queueLock(m_queueMutex);
    if (m_queuedMessages.size() >= m_maximumQueuedMessages) {
      return false;
    }
    m_queuedMessages.push_back(std::move(receivedApplicationMessage));
  }
  m_messageAvailable.notify_one();
  return true;
}

std::optional<ReceivedApplicationMessage>
ApplicationMessageQueue::waitAndPopMessage(std::chrono::milliseconds maximumWaitTime) {
  std::unique_lock<std::mutex> queueLock(m_queueMutex);
  m_messageAvailable.wait_for(queueLock, maximumWaitTime, [this] { return !m_queuedMessages.empty(); });
  if (m_queuedMessages.empty()) {
    return std::nullopt;
  }

  auto receivedApplicationMessage = std::move(m_queuedMessages.front());
  m_queuedMessages.pop_front();
  return receivedApplicationMessage;
}

} // namespace messaging
} // namespace iot
