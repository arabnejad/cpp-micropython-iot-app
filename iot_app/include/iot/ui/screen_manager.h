#pragma once

#include "iot/display/display_types.h"
#include "iot/logging/logger.h"
#include "iot/ui/render_backend.h"
#include "iot/ui/ui_types.h"

#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace iot {
namespace ui {

/**
 * Owns the renderer and sends all drawing work to one render thread.
 *
 * Python and the main loop can request drawing, but only this class's render
 * thread calls LVGL. The queue has a fixed limit so an application cannot use
 * all available memory by drawing faster than the screen can update.
 */
class ScreenManager {
public:
  ScreenManager(display::ActiveDisplay activeDisplay, std::unique_ptr<IRenderBackend> renderBackend,
                std::size_t maximumPendingCommands);
  ~ScreenManager();

  // Owns the backend, render thread, and queue; copying and moving are disabled.
  ScreenManager(const ScreenManager &)            = delete;
  ScreenManager &operator=(const ScreenManager &) = delete;
  ScreenManager(ScreenManager &&)                 = delete;
  ScreenManager &operator=(ScreenManager &&)      = delete;

  /** Starts the render thread and waits until the backend is ready. */
  void start();
  /** Stops the render thread. Calling this more than once is safe. */
  void stop() noexcept;
  /** Queues a new text box and returns the ID used to update it later. */
  WidgetId drawTextBox(const TextBoxSpec &textBoxSpec);
  /** Queues new text for a box created by `drawTextBox()`. */
  void updateTextBox(WidgetId textBoxId, std::string updatedText);
  /** Queues a new screen position for an existing text box. */
  void moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y);
  /** Queues deletion of an existing text box. */
  void deleteTextBox(WidgetId textBoxId);
  /** Queues a solid rectangle. */
  void fillArea(const FilledAreaSpec &filledAreaSpec);
  /** Queues removal of application widgets and displays an emergency screen. */
  void showErrorScreen(const TextBoxSpec &errorBoxSpec);
  /** Queues removal of the current widgets and a new background colour. */
  void clear(Color screenBackgroundColor);

  /**
   * Rethrows an error that stopped the render thread.
   *
   * The main loop calls this regularly. This prevents the application from
   * continuing as if drawing still worked after the render thread has stopped.
   */
  void throwIfRenderThreadFailed() const;

private:
  using RenderCommand = std::function<void(IRenderBackend &)>;

  /** Adds one drawing operation to the bounded render queue. */
  void enqueueRenderCommand(RenderCommand command);
  /** Runs the backend and queued drawing operations on the render thread. */
  void runRenderLoop(std::promise<void> initialization) noexcept;
  /** Stores an exception raised by the render thread. */
  void storeRenderThreadFailure(std::exception_ptr failure) noexcept;
  /** Rethrows the saved render-thread error. The caller holds the state lock. */
  void rethrowRenderThreadFailureWhileLocked() const;
  /** Checks that drawing is available. The caller holds the state lock. */
  void throwIfRenderThreadIsUnavailableWhileLocked() const;

  logging::Logger                 logger_{"ScreenManager"};
  display::ActiveDisplay          activeDisplay_;
  std::unique_ptr<IRenderBackend> renderBackend_;
  const std::size_t               maximumPendingCommands_;
  // Widget creation is requested only by the main/MicroPython thread.
  WidgetId                  nextWidgetId_{1U};
  mutable std::mutex        renderStateMutex_;
  std::condition_variable   renderCommandAvailable_;
  std::queue<RenderCommand> pendingRenderCommands_;
  std::thread               renderThread_;
  std::exception_ptr        renderThreadFailure_;
  bool                      stopping_{false};
};

} // namespace ui
} // namespace iot
