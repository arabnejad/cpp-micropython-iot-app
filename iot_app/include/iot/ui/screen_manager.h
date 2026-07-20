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
#include <unordered_map>

namespace iot {
namespace ui {

namespace internal {
class JpegImageLoader;
}

/*
 * Owns the renderer and its render thread.
 *
 * Drawing requests from Python and the main loop are placed in a queue. The
 * render thread takes requests from this queue and is the only thread that
 * calls LVGL.
 *
 * Call the public drawing methods from the main/MicroPython thread. That
 * thread also owns widget IDs, image state, and the JPEG cache. A JPEG cache
 * miss is decoded there before its render command is queued, so that call
 * waits for decoding to finish. The render thread keeps running meanwhile.
 * The queue lock protects communication with the renderer, not these other
 * main-thread objects.
 *
 * The queue has a fixed maximum size. If the application sends drawing
 * requests faster than the render thread can process them, the queue becomes
 * full and ScreenManager rejects the next request with an error. This prevents
 * drawing requests from using more and more memory.
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

  /* Starts the render thread. */
  void start();
  /* Stops the render thread. */
  void stop() noexcept;
  /* Queues a new text box and returns the ID used to update it later. */
  WidgetId drawTextBox(const TextBoxSpec &textBoxSpec);
  void     updateTextBox(WidgetId textBoxId, std::string updatedText);
  void     moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y);
  void     deleteTextBox(WidgetId textBoxId);
  /* Decodes and queues a JPEG image. The returned ID can change it later. */
  WidgetId drawJpegImage(const JpegImageSpec &jpegImageSpec);
  void     replaceJpegImage(WidgetId imageId, std::filesystem::path jpegFilePath);
  void     moveJpegImage(WidgetId imageId, std::int32_t x, std::int32_t y);
  void     setJpegImageScale(WidgetId imageId, std::uint16_t scalePercent);
  void     deleteJpegImage(WidgetId imageId);
  /* Decodes a JPEG and places it behind the other widgets. */
  void setBackgroundJpegImage(const BackgroundJpegImageSpec &backgroundJpegImageSpec);
  void clearBackgroundJpegImage();
  /* Queues a solid rectangle. */
  void fillArea(const FilledAreaSpec &filledAreaSpec);
  /* Removes application widgets and shows the emergency screen. */
  void showErrorScreen(const TextBoxSpec &errorBoxSpec);
  /* Removes the current widgets and applies a background colour. */
  void clear(Color screenBackgroundColor);

  /*
   * Rethrows an error that stopped the render thread.
   *
   * The main loop calls this regularly so a render failure does not go
   * unnoticed.
   */
  void throwIfRenderThreadFailed() const;

private:
  using RenderCommand = std::function<void(IRenderBackend &)>;

  /* Values needed when an existing image is loaded again at a new size. */
  struct JpegImageSourceState {
    std::filesystem::path jpegFilePath;
    std::uint16_t         requestedScalePercent{100U};
  };

  /* Adds a drawing operation to the bounded queue. */
  void enqueueRenderCommand(RenderCommand command);
  /* Discards queued drawing work and makes this the next render command. */
  void replacePendingRenderCommandsWith(RenderCommand replacementCommand);
  /* Runs the backend and queued commands on the render thread. */
  void runRenderLoop(std::promise<void> initialization) noexcept;
  /* Saves an exception raised by the render thread. */
  void storeRenderThreadFailure(std::exception_ptr failure) noexcept;
  /* Rethrows the saved render-thread error. The caller holds the state lock. */
  void rethrowRenderThreadFailureWhileLocked() const;
  /* Checks that drawing is available. The caller holds the state lock. */
  void                  throwIfRenderThreadIsUnavailableWhileLocked() const;
  static void           validateImageScalePercent(std::uint16_t scalePercent);
  JpegImageSourceState &findJpegImageSourceState(WidgetId imageId);

  logging::Logger                            m_logger{"ScreenManager"};
  display::ActiveDisplay                     m_activeDisplay;
  std::unique_ptr<IRenderBackend>            m_renderBackend;
  const std::size_t                          m_maximumPendingCommands;
  std::unique_ptr<internal::JpegImageLoader> m_jpegImageLoader;
  // Widget creation is requested only by the main/MicroPython thread.
  WidgetId                                           m_nextWidgetId{1U};
  std::unordered_map<WidgetId, JpegImageSourceState> m_jpegImageSourceStatesById;
  mutable std::mutex                                 m_renderStateMutex;
  std::condition_variable                            m_renderCommandAvailable;
  std::queue<RenderCommand>                          m_pendingRenderCommands;
  std::thread                                        m_renderThread;
  std::exception_ptr                                 m_renderThreadFailure;
  bool                                               m_stopping{false};
};

} // namespace ui
} // namespace iot
