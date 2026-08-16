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
    : activeDisplay_(std::move(activeDisplay)), renderBackend_(std::move(renderBackend)),
      maximumPendingCommands_(maximumPendingCommands) {
  if (!renderBackend_) {
    IOT_LOG_ERROR(logger_, "Cannot create ScreenManager because the render backend is null");
    throw std::invalid_argument("ScreenManager requires a render backend");
  }
  if (maximumPendingCommands_ == 0U) {
    IOT_LOG_ERROR(logger_, "Cannot create ScreenManager because maximumPendingCommands is zero");
    throw std::invalid_argument("ScreenManager requires a non-zero command queue limit");
  }
}

ScreenManager::~ScreenManager() {
  stop();
}

void ScreenManager::start() {
  IOT_LOG_DEBUG(logger_, "Starting render thread; queue limit=", maximumPendingCommands_,
                ", display=", activeDisplay_.mode().width, 'x', activeDisplay_.mode().height);
  std::future<void> initialized;
  {
    std::lock_guard<std::mutex> lock(renderStateMutex_);
    if (renderThread_.joinable()) {
      rethrowRenderThreadFailureWhileLocked();
      return;
    }
    stopping_            = false;
    renderThreadFailure_ = nullptr;
    std::promise<void> initialization;
    initialized   = initialization.get_future();
    renderThread_ = std::thread(&ScreenManager::runRenderLoop, this, std::move(initialization));
  }

  try {
    initialized.get();
    IOT_LOG_INFO(logger_, "Render thread started successfully");
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(logger_, "Render thread failed during startup: ", error.what(),
                  "; display=", activeDisplay_.mode().width, 'x', activeDisplay_.mode().height);
    stop();
    throw;
  } catch (...) {
    IOT_LOG_ERROR(logger_, "Render thread failed during startup with an unknown exception");
    stop();
    throw;
  }
}

void ScreenManager::stop() noexcept {
  {
    std::lock_guard<std::mutex> lock(renderStateMutex_);
    if (!renderThread_.joinable()) {
      return;
    }
    stopping_ = true;
  }
  renderCommandAvailable_.notify_one();
  renderThread_.join();

  std::lock_guard<std::mutex> lock(renderStateMutex_);
  std::queue<RenderCommand>   emptyQueue;
  pendingRenderCommands_.swap(emptyQueue);
  IOT_LOG_INFO(logger_, "Render thread stopped");
}

WidgetId ScreenManager::drawTextBox(const TextBoxSpec &textBoxSpec) {
  const WidgetId textBoxId = nextWidgetId_++;
  logTextBoxRequest(logger_, textBoxId, textBoxSpec);
  enqueueRenderCommand(
      [textBoxId, textBoxSpec](IRenderBackend &backend) { backend.createTextBox(textBoxId, textBoxSpec); });
  return textBoxId;
}

void ScreenManager::fillArea(const FilledAreaSpec &filledAreaSpec) {
  IOT_LOG_DEBUG(logger_, "Queueing filled area bounds={x=", filledAreaSpec.bounds.x, ", y=", filledAreaSpec.bounds.y,
                ", width=", filledAreaSpec.bounds.width, ", height=", filledAreaSpec.bounds.height, "}, color=rgb(",
                static_cast<unsigned int>(filledAreaSpec.color.red), ',',
                static_cast<unsigned int>(filledAreaSpec.color.green), ',',
                static_cast<unsigned int>(filledAreaSpec.color.blue), ')');
  enqueueRenderCommand([filledAreaSpec](IRenderBackend &backend) { backend.fillArea(filledAreaSpec); });
}

void ScreenManager::showErrorScreen(const TextBoxSpec &errorBoxSpec) {
  IOT_LOG_ERROR(logger_, "Queueing runtime error screen; text='", textPreviewForLog(errorBoxSpec.text),
                "', bounds={x=", errorBoxSpec.bounds.x, ", y=", errorBoxSpec.bounds.y,
                ", width=", errorBoxSpec.bounds.width, ", height=", errorBoxSpec.bounds.height,
                "}, backgroundColor=rgb(", static_cast<unsigned int>(errorBoxSpec.backgroundColor.red), ',',
                static_cast<unsigned int>(errorBoxSpec.backgroundColor.green), ',',
                static_cast<unsigned int>(errorBoxSpec.backgroundColor.blue), ')');
  enqueueRenderCommand([errorBoxSpec](IRenderBackend &backend) { backend.showErrorScreen(errorBoxSpec); });
}

void ScreenManager::updateTextBox(WidgetId textBoxId, std::string updatedText) {
  IOT_LOG_DEBUG(logger_, "Queueing text update for id=", textBoxId, ", text='", textPreviewForLog(updatedText), "'");
  enqueueRenderCommand([textBoxId, updatedText = std::move(updatedText)](IRenderBackend &backend) {
    backend.updateTextBox(textBoxId, updatedText);
  });
}

void ScreenManager::moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y) {
  IOT_LOG_DEBUG(logger_, "Queueing text-box move for id=", textBoxId, ", x=", x, ", y=", y);
  enqueueRenderCommand([textBoxId, x, y](IRenderBackend &backend) { backend.moveTextBox(textBoxId, x, y); });
}

void ScreenManager::deleteTextBox(WidgetId textBoxId) {
  IOT_LOG_DEBUG(logger_, "Queueing text-box deletion for id=", textBoxId);
  enqueueRenderCommand([textBoxId](IRenderBackend &backend) { backend.deleteTextBox(textBoxId); });
}

void ScreenManager::clear(Color screenBackgroundColor) {
  IOT_LOG_DEBUG(logger_, "Queueing screen clear; color=rgb(", static_cast<unsigned int>(screenBackgroundColor.red), ',',
                static_cast<unsigned int>(screenBackgroundColor.green), ',',
                static_cast<unsigned int>(screenBackgroundColor.blue), ')');
  {
    std::lock_guard<std::mutex> lock(renderStateMutex_);
    throwIfRenderThreadIsUnavailableWhileLocked();

    // A clear starts a new application screen. Drop drawing commands still
    // waiting from the previous app.
    std::queue<RenderCommand> discardedCommands;
    pendingRenderCommands_.swap(discardedCommands);
    pendingRenderCommands_.push(
        [screenBackgroundColor](IRenderBackend &backend) { backend.clear(screenBackgroundColor); });
  }
  renderCommandAvailable_.notify_one();
}

void ScreenManager::throwIfRenderThreadFailed() const {
  std::lock_guard<std::mutex> lock(renderStateMutex_);
  rethrowRenderThreadFailureWhileLocked();
}

void ScreenManager::rethrowRenderThreadFailureWhileLocked() const {
  if (renderThreadFailure_) {
    std::rethrow_exception(renderThreadFailure_);
  }
}

void ScreenManager::throwIfRenderThreadIsUnavailableWhileLocked() const {
  rethrowRenderThreadFailureWhileLocked();
  if (!renderThread_.joinable() || stopping_) {
    IOT_LOG_ERROR(logger_, "Drawing rejected; renderThreadJoinable=", renderThread_.joinable(),
                  ", stopping=", stopping_, ", pendingCommands=", pendingRenderCommands_.size());
    throw std::logic_error("ScreenManager must be running before drawing");
  }
}

void ScreenManager::enqueueRenderCommand(RenderCommand command) {
  {
    std::lock_guard<std::mutex> lock(renderStateMutex_);
    throwIfRenderThreadIsUnavailableWhileLocked();
    if (pendingRenderCommands_.size() >= maximumPendingCommands_) {
      IOT_LOG_ERROR(logger_, "Render command queue is full; pendingCommands=", pendingRenderCommands_.size(),
                    ", queueLimit=", maximumPendingCommands_);
      throw std::runtime_error("ScreenManager command queue is full; the application is drawing too quickly");
    }
    pendingRenderCommands_.push(std::move(command));
  }
  renderCommandAvailable_.notify_one();
}

void ScreenManager::storeRenderThreadFailure(std::exception_ptr failure) noexcept {
  std::lock_guard<std::mutex> lock(renderStateMutex_);
  renderThreadFailure_ = std::move(failure);
}

void ScreenManager::runRenderLoop(std::promise<void> initialization) noexcept {
  bool backendWasInitialized = false;
  try {
    renderBackend_->initialize(activeDisplay_);
    backendWasInitialized = true;
    initialization.set_value();

    while (true) {
      while (true) {
        RenderCommand command;
        {
          std::lock_guard<std::mutex> lock(renderStateMutex_);
          if (pendingRenderCommands_.empty()) {
            break;
          }
          command = std::move(pendingRenderCommands_.front());
          pendingRenderCommands_.pop();
        }
        command(*renderBackend_);
      }

      const std::uint32_t requestedWaitMilliseconds = renderBackend_->processEventsAndGetWaitMilliseconds();
      const std::uint32_t waitMilliseconds =
          std::min<std::uint32_t>(50U, std::max<std::uint32_t>(1U, requestedWaitMilliseconds));

      std::unique_lock<std::mutex> lock(renderStateMutex_);
      if (stopping_) {
        break;
      }
      renderCommandAvailable_.wait_for(lock, std::chrono::milliseconds(waitMilliseconds),
                                       [this] { return stopping_ || !pendingRenderCommands_.empty(); });
      if (stopping_) {
        break;
      }
    }
  } catch (...) {
    const auto  failure                  = std::current_exception();
    std::size_t pendingCommandsAtFailure = 0U;
    {
      std::lock_guard<std::mutex> lock(renderStateMutex_);
      pendingCommandsAtFailure = pendingRenderCommands_.size();
    }
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception &error) {
      IOT_LOG_ERROR(logger_, "Render thread stopped after an exception: ", error.what(),
                    "; pendingCommands=", pendingCommandsAtFailure);
    } catch (...) {
      IOT_LOG_ERROR(logger_,
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
  renderBackend_->shutdown();
}

} // namespace ui
} // namespace iot
