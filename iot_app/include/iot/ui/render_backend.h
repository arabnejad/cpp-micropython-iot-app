#pragma once

#include "iot/display/display_types.h"
#include "iot/ui/ui_types.h"

#include <cstdint>
#include <memory>

namespace iot {
namespace ui {

class ILvglFramebufferDriver;

/** Drawing operations that ScreenManager runs on its render thread. */
class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;

  // Owns one rendering context; copying and moving are disabled.
  IRenderBackend(const IRenderBackend &)            = delete;
  IRenderBackend &operator=(const IRenderBackend &) = delete;
  IRenderBackend(IRenderBackend &&)                 = delete;
  IRenderBackend &operator=(IRenderBackend &&)      = delete;

  /** Opens the output and prepares it for drawing. */
  virtual void initialize(const display::ActiveDisplay &activeDisplay) = 0;
  /** Releases the output. It is safe to call this more than once. */
  virtual void shutdown() noexcept = 0;
  /** Creates a text box and associates it with `textBoxId`. */
  virtual void createTextBox(WidgetId textBoxId, const TextBoxSpec &textBoxSpec) = 0;
  /** Changes the text in an existing text box. */
  virtual void updateTextBox(WidgetId textBoxId, const std::string &updatedText) = 0;
  /** Moves an existing text box without creating a new widget. */
  virtual void moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y) = 0;
  /** Deletes an existing text box and its label. */
  virtual void deleteTextBox(WidgetId textBoxId) = 0;
  /** Draws one solid rectangle. */
  virtual void fillArea(const FilledAreaSpec &filledAreaSpec) = 0;
  /** Clears application widgets and shows the runtime-owned error screen. */
  virtual void showErrorScreen(const TextBoxSpec &errorBoxSpec) = 0;
  /** Removes all widgets and fills the screen with one colour. */
  virtual void clear(Color screenBackgroundColor) = 0;
  /** Updates the display and returns how many milliseconds it can wait. */
  virtual std::uint32_t processEventsAndGetWaitMilliseconds() = 0;

protected:
  IRenderBackend() = default;
};

/** Creates the LVGL backend that draws through Linux `/dev/fb0`. */
std::unique_ptr<IRenderBackend> makeLvglFramebufferRenderBackend();

/** Creates the LVGL backend with a supplied framebuffer driver. */
std::unique_ptr<IRenderBackend>
makeLvglFramebufferRenderBackend(std::unique_ptr<ILvglFramebufferDriver> framebufferDriver);

} // namespace ui
} // namespace iot
