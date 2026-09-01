#pragma once

#include "iot/display/display_types.h"
#include "iot/ui/ui_types.h"

#include <cstdint>
#include <memory>

namespace iot {
namespace ui {

/* Drawing operations executed by the ScreenManager render thread. */
class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;

  // Owns one rendering context; copying and moving are disabled.
  IRenderBackend(const IRenderBackend &)            = delete;
  IRenderBackend &operator=(const IRenderBackend &) = delete;
  IRenderBackend(IRenderBackend &&)                 = delete;
  IRenderBackend &operator=(IRenderBackend &&)      = delete;

  /* Opens the output and prepares LVGL. */
  virtual void initialize(const display::ActiveDisplay &activeDisplay) = 0;
  /* Releases the output. */
  virtual void shutdown() noexcept                                               = 0;
  virtual void createTextBox(WidgetId textBoxId, const TextBoxSpec &textBoxSpec) = 0;
  virtual void updateTextBox(WidgetId textBoxId, const std::string &updatedText) = 0;
  virtual void moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y)   = 0;
  virtual void deleteTextBox(WidgetId textBoxId)                                 = 0;
  virtual void fillArea(const FilledAreaSpec &filledAreaSpec)                    = 0;
  /* Clears application widgets and shows the runtime-owned error screen. */
  virtual void showErrorScreen(const TextBoxSpec &errorBoxSpec) = 0;
  /* Removes all widgets and fills the screen with one colour. */
  virtual void clear(Color screenBackgroundColor) = 0;
  /* Lets LVGL update the display and returns its requested wait time. */
  virtual std::uint32_t processEventsAndGetWaitMilliseconds() = 0;

protected:
  IRenderBackend() = default;
};

/*
 * Creates the render backend used by main.cpp. The factory hides the LVGL
 * framebuffer implementation and returns the IRenderBackend object required
 * by ScreenManager.
 */
std::unique_ptr<IRenderBackend> makeLvglFramebufferRenderBackend();

} // namespace ui
} // namespace iot
