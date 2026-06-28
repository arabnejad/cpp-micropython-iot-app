#pragma once

#include "iot/ui/render_backend.h"

#include <memory>

struct _lv_display_t;
typedef struct _lv_display_t lv_display_t;

namespace iot {
namespace ui {
namespace internal {

/*
 * Internal LVGL framebuffer operations used by the render backend.
 *
 * The normal implementation creates an LVGL display and opens /dev/fb0. Unit
 * tests replace these two operations so rendering behaviour can be checked
 * without a real framebuffer. Application drawing code should use
 * ScreenManager and IRenderBackend instead.
 */
class ILvglFramebufferDriver {
public:
  virtual ~ILvglFramebufferDriver() = default;

  /* Creates the LVGL display object used by the render backend. */
  virtual lv_display_t *createDisplay() = 0;

  /* Opens a framebuffer device such as /dev/fb0 for the display. */
  virtual bool openFramebuffer(lv_display_t *lvglDisplay, const char *framebufferDevicePath) = 0;
};

/* Creates the backend with a framebuffer driver supplied by a unit test. */
std::unique_ptr<IRenderBackend>
makeLvglFramebufferRenderBackend(std::unique_ptr<ILvglFramebufferDriver> framebufferDriver);

} // namespace internal
} // namespace ui
} // namespace iot
