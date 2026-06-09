#include "iot/ui/screen_manager.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace iot {
namespace ui {
namespace {

std::string textPreviewForLog(std::string text) {
  constexpr std::size_t maximumPreviewSize = 120U;
  for (char &character : text) {
    if (character == '\n' || character == '\r' || character == '\t') {
      character = ' ';
    }
  }
  if (text.size() > maximumPreviewSize) {
    text.resize(maximumPreviewSize);
    text += "...";
  }
  return text;
}

void logTextBoxRequest(logging::Logger &logger, WidgetId textBoxId, const TextBoxSpec &textBoxSpec) {
  IOT_LOG_DEBUG(logger, "Queueing text box id=", textBoxId, ", bounds={x=", textBoxSpec.bounds.x,
                ", y=", textBoxSpec.bounds.y, ", width=", textBoxSpec.bounds.width,
                ", height=", textBoxSpec.bounds.height, "}, text='", textPreviewForLog(textBoxSpec.text),
                "', fontSize=", textBoxSpec.fontSize,
                ", backgroundOpacity=", static_cast<unsigned int>(textBoxSpec.backgroundOpacity),
                ", borderWidth=", textBoxSpec.borderWidth);
}

} // namespace

ScreenManager::ScreenManager(display::ActiveDisplay activeDisplay, std::unique_ptr<IRenderBackend> renderBackend,
                             std::size_t maximumPendingCommands)
    : m_activeDisplay(std::move(activeDisplay)), m_renderBackend(std::move(renderBackend)),
      m_maximumPendingCommands(maximumPendingCommands) {
  if (!m_renderBackend) {
    IOT_LOG_ERROR(m_logger, "Cannot create ScreenManager because the render backend is null");
    throw std::invalid_argument("ScreenManager requires a render backend");
  }
  if (m_maximumPendingCommands == 0U) {
    IOT_LOG_ERROR(m_logger, "Cannot create ScreenManager because maximumPendingCommands is zero");
    throw std::invalid_argument("ScreenManager requires a non-zero command queue limit");
  }
}

ScreenManager::~ScreenManager() {
  stop();
}

void ScreenManager::start() {
  IOT_LOG_DEBUG(m_logger, "Starting render thread; queue limit=", m_maximumPendingCommands,
                ", display=", m_activeDisplay.mode().width, 'x', m_activeDisplay.mode().height);
  std::future<void> initialized;
  {
    std::lock_guard<std::mutex> lock(m_renderStateMutex);
    if (m_renderThread.joinable()) {
      rethrowRenderThreadFailureWhileLocked();
      return;
    }
    m_stopping            = false;
    m_renderThreadFailure = nullptr;
    std::promise<void> initialization;
    initialized    = initialization.get_future();
    m_renderThread = std::thread(&ScreenManager::runRenderLoop, this, std::move(initialization));
  }

  try {
    // waits until the backend is ready
    initialized.get();
    IOT_LOG_INFO(m_logger, "Render thread started successfully");
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(m_logger, "Render thread failed during startup: ", error.what(),
                  "; display=", m_activeDisplay.mode().width, 'x', m_activeDisplay.mode().height);
    stop();
    throw;
  } catch (...) {
    IOT_LOG_ERROR(m_logger, "Render thread failed during startup with an unknown exception");
    stop();
    throw;
  }
}

void ScreenManager::stop() noexcept {
  {
    std::lock_guard<std::mutex> lock(m_renderStateMutex);
    if (!m_renderThread.joinable()) {
      return;
    }
    m_stopping = true;
  }
  m_renderCommandAvailable.notify_one();
  m_renderThread.join();

  std::lock_guard<std::mutex> lock(m_renderStateMutex);
  std::queue<RenderCommand>   emptyQueue;
  m_pendingRenderCommands.swap(emptyQueue);
  IOT_LOG_INFO(m_logger, "Render thread stopped");
}

WidgetId ScreenManager::drawTextBox(const TextBoxSpec &textBoxSpec) {
  const WidgetId textBoxId = m_nextWidgetId++;
  logTextBoxRequest(m_logger, textBoxId, textBoxSpec);
  enqueueRenderCommand(
      [textBoxId, textBoxSpec](IRenderBackend &backend) { backend.createTextBox(textBoxId, textBoxSpec); });
  return textBoxId;
}

void ScreenManager::fillArea(const FilledAreaSpec &filledAreaSpec) {
  IOT_LOG_DEBUG(m_logger, "Queueing filled area bounds={x=", filledAreaSpec.bounds.x, ", y=", filledAreaSpec.bounds.y,
                ", width=", filledAreaSpec.bounds.width, ", height=", filledAreaSpec.bounds.height, "}, color=rgb(",
                static_cast<unsigned int>(filledAreaSpec.color.red), ',',
                static_cast<unsigned int>(filledAreaSpec.color.green), ',',
                static_cast<unsigned int>(filledAreaSpec.color.blue), ')');
  enqueueRenderCommand([filledAreaSpec](IRenderBackend &backend) { backend.fillArea(filledAreaSpec); });
}

void ScreenManager::showErrorScreen(const TextBoxSpec &errorBoxSpec) {
  IOT_LOG_ERROR(m_logger, "Queueing runtime error screen; text='", textPreviewForLog(errorBoxSpec.text),
                "', bounds={x=", errorBoxSpec.bounds.x, ", y=", errorBoxSpec.bounds.y,
                ", width=", errorBoxSpec.bounds.width, ", height=", errorBoxSpec.bounds.height,
                "}, backgroundColor=rgb(", static_cast<unsigned int>(errorBoxSpec.backgroundColor.red), ',',
                static_cast<unsigned int>(errorBoxSpec.backgroundColor.green), ',',
                static_cast<unsigned int>(errorBoxSpec.backgroundColor.blue), ')');
  enqueueRenderCommand([errorBoxSpec](IRenderBackend &backend) { backend.showErrorScreen(errorBoxSpec); });
}

void ScreenManager::updateTextBox(WidgetId textBoxId, std::string updatedText) {
  IOT_LOG_DEBUG(m_logger, "Queueing text update for id=", textBoxId, ", text='", textPreviewForLog(updatedText), "'");
  enqueueRenderCommand([textBoxId, updatedText = std::move(updatedText)](IRenderBackend &backend) {
    backend.updateTextBox(textBoxId, updatedText);
  });
}

void ScreenManager::moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y) {
  IOT_LOG_DEBUG(m_logger, "Queueing text-box move for id=", textBoxId, ", x=", x, ", y=", y);
  enqueueRenderCommand([textBoxId, x, y](IRenderBackend &backend) { backend.moveTextBox(textBoxId, x, y); });
}

void ScreenManager::deleteTextBox(WidgetId textBoxId) {
  IOT_LOG_DEBUG(m_logger, "Queueing text-box deletion for id=", textBoxId);
  enqueueRenderCommand([textBoxId](IRenderBackend &backend) { backend.deleteTextBox(textBoxId); });
}

void ScreenManager::clear(Color screenBackgroundColor) {
  IOT_LOG_DEBUG(m_logger, "Queueing screen clear; color=rgb(", static_cast<unsigned int>(screenBackgroundColor.red),
                ',', static_cast<unsigned int>(screenBackgroundColor.green), ',',
                static_cast<unsigned int>(screenBackgroundColor.blue), ')');
  {
    std::lock_guard<std::mutex> lock(m_renderStateMutex);
    throwIfRenderThreadIsUnavailableWhileLocked();

    // A clear starts a new application screen. Drop drawing commands still
    // waiting from the previous app.
    std::queue<RenderCommand> discardedCommands;
    m_pendingRenderCommands.swap(discardedCommands);
    m_pendingRenderCommands.push(
        [screenBackgroundColor](IRenderBackend &backend) { backend.clear(screenBackgroundColor); });
  }
  m_renderCommandAvailable.notify_one();
}

void ScreenManager::throwIfRenderThreadFailed() const {
  std::lock_guard<std::mutex> lock(m_renderStateMutex);
  rethrowRenderThreadFailureWhileLocked();
}

void ScreenManager::rethrowRenderThreadFailureWhileLocked() const {
  if (m_renderThreadFailure) {
    std::rethrow_exception(m_renderThreadFailure);
  }
}

void ScreenManager::throwIfRenderThreadIsUnavailableWhileLocked() const {
  rethrowRenderThreadFailureWhileLocked();
  if (!m_renderThread.joinable() || m_stopping) {
    IOT_LOG_ERROR(m_logger, "Drawing rejected; renderThreadJoinable=", m_renderThread.joinable(),
                  ", stopping=", m_stopping, ", pendingCommands=", m_pendingRenderCommands.size());
    throw std::logic_error("ScreenManager must be running before drawing");
  }
}

void ScreenManager::enqueueRenderCommand(RenderCommand command) {
  {
    std::lock_guard<std::mutex> lock(m_renderStateMutex);
    throwIfRenderThreadIsUnavailableWhileLocked();
    if (m_pendingRenderCommands.size() >= m_maximumPendingCommands) {
      IOT_LOG_ERROR(m_logger, "Render command queue is full; pendingCommands=", m_pendingRenderCommands.size(),
                    ", queueLimit=", m_maximumPendingCommands);
      throw std::runtime_error("ScreenManager command queue is full; the application is drawing too quickly");
    }
    m_pendingRenderCommands.push(std::move(command));
  }
  m_renderCommandAvailable.notify_one();
}

void ScreenManager::storeRenderThreadFailure(std::exception_ptr failure) noexcept {
  std::lock_guard<std::mutex> lock(m_renderStateMutex);
  m_renderThreadFailure = std::move(failure);
}

void ScreenManager::runRenderLoop(std::promise<void> initialization) noexcept {
  bool backendWasInitialized = false;
  try {
    m_renderBackend->initialize(m_activeDisplay);
    backendWasInitialized = true;
    initialization.set_value();

    while (true) {
      while (true) {
        RenderCommand command;
        {
          std::lock_guard<std::mutex> lock(m_renderStateMutex);
          if (m_pendingRenderCommands.empty()) {
            break;
          }
          command = std::move(m_pendingRenderCommands.front());
          m_pendingRenderCommands.pop();
        }
        command(*m_renderBackend);
      }

      const std::uint32_t requestedWaitMilliseconds = m_renderBackend->processEventsAndGetWaitMilliseconds();
      const std::uint32_t waitMilliseconds =
          std::min<std::uint32_t>(50U, std::max<std::uint32_t>(1U, requestedWaitMilliseconds));

      std::unique_lock<std::mutex> lock(m_renderStateMutex);
      if (m_stopping) {
        break;
      }
      m_renderCommandAvailable.wait_for(lock, std::chrono::milliseconds(waitMilliseconds),
                                        [this] { return m_stopping || !m_pendingRenderCommands.empty(); });
      if (m_stopping) {
        break;
      }
    }
  } catch (...) {
    const auto  failure                  = std::current_exception();
    std::size_t pendingCommandsAtFailure = 0U;
    {
      std::lock_guard<std::mutex> lock(m_renderStateMutex);
      pendingCommandsAtFailure = m_pendingRenderCommands.size();
    }
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception &error) {
      IOT_LOG_ERROR(m_logger, "Render thread stopped after an exception: ", error.what(),
                    "; pendingCommands=", pendingCommandsAtFailure);
    } catch (...) {
      IOT_LOG_ERROR(m_logger,
                    "Render thread stopped after an unknown exception; pendingCommands=", pendingCommandsAtFailure);
    }
    storeRenderThreadFailure(failure);
    if (!backendWasInitialized) {
      try {
        initialization.set_exception(failure);
      } catch (...) {
      }
    }
  }
  m_renderBackend->shutdown();
}

} // namespace ui
} // namespace iot
