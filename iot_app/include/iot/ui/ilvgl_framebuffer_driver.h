#pragma once

struct _lv_display_t;
typedef struct _lv_display_t lv_display_t;

namespace iot {
namespace ui {

/** Opens an LVGL display on a Linux framebuffer device. */
class ILvglFramebufferDriver {
public:
  virtual ~ILvglFramebufferDriver() = default;

  /** Creates the LVGL display object used by the render backend. */
  virtual lv_display_t *createDisplay() = 0;

  /** Connects the display to a device such as `/dev/fb0`. */
  virtual bool openFramebuffer(lv_display_t *lvglDisplay, const char *framebufferDevicePath) = 0;
};

} // namespace ui
} // namespace iot
