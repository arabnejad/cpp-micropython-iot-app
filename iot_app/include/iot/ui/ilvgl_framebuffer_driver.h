#pragma once

struct _lv_display_t;
typedef struct _lv_display_t lv_display_t;

namespace iot {
namespace ui {

/*
 * Connects an LVGL display to a Linux framebuffer device such as /dev/fb0.
 *
 * The render backend uses this interface during startup to create the LVGL
 * display and open the framebuffer. Application drawing code does not call it
 * directly; drawing requests go through ScreenManager.
 */
class ILvglFramebufferDriver {
public:
  virtual ~ILvglFramebufferDriver() = default;

  /* Creates the LVGL display object used by the render backend. */
  virtual lv_display_t *createDisplay() = 0;

  /* Opens a framebuffer device such as /dev/fb0 for the display. */
  virtual bool openFramebuffer(lv_display_t *lvglDisplay, const char *framebufferDevicePath) = 0;
};

} // namespace ui
} // namespace iot
