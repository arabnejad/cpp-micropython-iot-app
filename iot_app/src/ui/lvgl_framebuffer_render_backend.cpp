/*
 * Draws through Linux /dev/fb0. Linux chooses the monitor mode before this
 * app starts, and LVGL uses the framebuffer at that same size. This app does
 * not change the monitor resolution.
 *
 * The framebuffer works without a desktop, window manager, Mesa, EGL, or
 * OpenGL. That fits Raspberry Pi OS console mode and a small Buildroot image.
 *
 * Examples using the same LVGL 9 framebuffer API:
 *
 * - LVGL's maintained Linux port, fbdev backend:
 *   https://github.com/lvgl/lv_port_linux/blob/738f6b217340f7472960dcbebb68ca632619c376/src/lib/display_backends/fbdev.c#L84-L95
 * - Official LVGL 9.5 Linux framebuffer documentation:
 *   https://github.com/lvgl/lvgl/blob/v9.5.0/docs/src/integration/embedded_linux/drivers/fbdev.rst#L17-L42
 * - Raspberry Pi 4 example using /dev/fb0 with the same API:
 *   https://forum.lvgl.io/t/i-faced-undefined-reference-to-lv-linux-fbdev-create-error/15131/2
 */

#include "iot/ui/render_backend.h"

#include "internal/ilvgl_framebuffer_driver.h"

#include <lvgl.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace iot {
namespace ui {
namespace {

/*
 * Linux exposes the console framebuffer through this device.
 *
 * Opening /dev/fb0 uses the mode Linux already selected. It does not change
 * the monitor mode like a direct DRM renderer could.
 */
constexpr const char *framebufferDevicePath = "/dev/fb0";

lv_color_t toLvglColor(Color color) {
  return lv_color_make(color.red, color.green, color.blue);
}

const lv_font_t *fontForSize(std::uint16_t requestedSize) {
  if (requestedSize <= 14U) {
    return &lv_font_montserrat_14;
  }
  if (requestedSize <= 20U) {
    return &lv_font_montserrat_20;
  }
  if (requestedSize <= 24U) {
    return &lv_font_montserrat_24;
  }
  return &lv_font_montserrat_32;
}

void validateBounds(const Rect &bounds) {
  if (bounds.width <= 0 || bounds.height <= 0) {
    throw std::invalid_argument("A drawable area must have positive width and height");
  }
}

/* Keeps the outer box and its text label together. */
struct TextBoxWidgets {
  lv_obj_t *box{nullptr};
  lv_obj_t *label{nullptr};
};

/* Draws LVGL widgets into the framebuffer already set up by Linux. */
class LvglFramebufferRenderBackend final : public IRenderBackend {
public:
  explicit LvglFramebufferRenderBackend(std::unique_ptr<internal::ILvglFramebufferDriver> framebufferDriver)
      : m_framebufferDriver(std::move(framebufferDriver)) {
    if (!m_framebufferDriver) {
      throw std::invalid_argument("LVGL render backend requires a framebuffer driver");
    }
  }

  ~LvglFramebufferRenderBackend() override {
    shutdown();
  }

  // Owns one LVGL display and its widgets; copying and moving are disabled.
  LvglFramebufferRenderBackend(const LvglFramebufferRenderBackend &)            = delete;
  LvglFramebufferRenderBackend &operator=(const LvglFramebufferRenderBackend &) = delete;
  LvglFramebufferRenderBackend(LvglFramebufferRenderBackend &&)                 = delete;
  LvglFramebufferRenderBackend &operator=(LvglFramebufferRenderBackend &&)      = delete;

  void initialize(const display::ActiveDisplay &activeDisplay) override {
    if (m_isInitialized) {
      throw std::logic_error("LVGL render backend is already initialized");
    }

    lv_init();
    m_isInitialized = true;
    m_lvglDisplay   = m_framebufferDriver->createDisplay();
    if (m_lvglDisplay == nullptr) {
      shutdown();
      throw std::runtime_error("LVGL could not create a Linux framebuffer display");
    }

    if (!m_framebufferDriver->openFramebuffer(m_lvglDisplay, framebufferDevicePath)) {
      shutdown();
      throw std::runtime_error(std::string{"LVGL could not open "} + framebufferDevicePath +
                               ". Check that the device exists and that this user has permission to write to it.");
    }
    lv_display_set_default(m_lvglDisplay);

    const auto framebufferWidth  = static_cast<std::uint32_t>(lv_display_get_horizontal_resolution(m_lvglDisplay));
    const auto framebufferHeight = static_cast<std::uint32_t>(lv_display_get_vertical_resolution(m_lvglDisplay));
    if (framebufferWidth != activeDisplay.mode().width || framebufferHeight != activeDisplay.mode().height) {
      const std::string framebufferSize = std::to_string(framebufferWidth) + "x" + std::to_string(framebufferHeight);
      const std::string activeDrmModeSize =
          std::to_string(activeDisplay.mode().width) + "x" + std::to_string(activeDisplay.mode().height);
      shutdown();
      throw std::runtime_error(std::string{framebufferDevicePath} + " is " + framebufferSize +
                               ", but the active DRM mode is " + activeDrmModeSize +
                               ". Configure the console framebuffer and DRM display to the same size.");
    }

    lv_obj_t *activeScreen = lv_screen_active();
    lv_obj_set_style_bg_color(activeScreen, lv_color_make(8, 13, 22), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(activeScreen, LV_OPA_COVER, LV_PART_MAIN);
  }

  void shutdown() noexcept override {
    m_textBoxes.clear();
    m_errorScreenLayer = nullptr;
    if (m_lvglDisplay != nullptr) {
      lv_display_delete(m_lvglDisplay);
      m_lvglDisplay = nullptr;
    }
    if (m_isInitialized) {
      lv_deinit();
      m_isInitialized = false;
    }
  }

  void createTextBox(WidgetId textBoxId, const TextBoxSpec &textBoxSpec) override {
    throwIfNotInitialized();

    lv_obj_t            *activeScreen   = lv_screen_active();
    const TextBoxWidgets textBoxWidgets = createTextBoxWidgets(activeScreen, textBoxSpec);
    lv_obj_set_style_text_align(textBoxWidgets.label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(textBoxWidgets.label);

    m_textBoxes[textBoxId] = textBoxWidgets;
  }

  void updateTextBox(WidgetId textBoxId, const std::string &updatedText) override {
    throwIfNotInitialized();
    const auto textBox = m_textBoxes.find(textBoxId);
    if (textBox == m_textBoxes.end()) {
      throw std::invalid_argument("The requested text-box widget does not exist");
    }
    lv_label_set_text(textBox->second.label, updatedText.c_str());
  }

  void moveTextBox(WidgetId textBoxId, std::int32_t x, std::int32_t y) override {
    throwIfNotInitialized();
    const auto textBox = m_textBoxes.find(textBoxId);
    if (textBox == m_textBoxes.end()) {
      throw std::invalid_argument("The requested text-box widget does not exist");
    }
    lv_obj_set_pos(textBox->second.box, x, y);
  }

  void deleteTextBox(WidgetId textBoxId) override {
    throwIfNotInitialized();
    const auto textBox = m_textBoxes.find(textBoxId);
    if (textBox == m_textBoxes.end()) {
      throw std::invalid_argument("The requested text-box widget does not exist");
    }

    // Deleting the outer box also deletes the label that LVGL created inside
    // it. Remove both stored pointers because neither object exists now.
    lv_obj_delete(textBox->second.box);
    m_textBoxes.erase(textBox);
  }

  void fillArea(const FilledAreaSpec &filledAreaSpec) override {
    throwIfNotInitialized();
    validateBounds(filledAreaSpec.bounds);

    lv_obj_t *filledAreaObject = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(filledAreaObject, filledAreaSpec.bounds.x, filledAreaSpec.bounds.y);
    lv_obj_set_size(filledAreaObject, filledAreaSpec.bounds.width, filledAreaSpec.bounds.height);
    lv_obj_remove_flag(filledAreaObject, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(filledAreaObject, toLvglColor(filledAreaSpec.color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(filledAreaObject, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(filledAreaObject, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(filledAreaObject, 0, LV_PART_MAIN);
  }

  void showErrorScreen(const TextBoxSpec &errorBoxSpec) override {
    throwIfNotInitialized();
    clear(errorBoxSpec.backgroundColor);

    // The top layer belongs to the runtime, so application widgets cannot
    // cover the error screen.
    m_errorScreenLayer = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(m_errorScreenLayer, 0, 0);
    lv_obj_set_size(m_errorScreenLayer, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(m_errorScreenLayer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(m_errorScreenLayer, toLvglColor(errorBoxSpec.backgroundColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_errorScreenLayer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(m_errorScreenLayer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(m_errorScreenLayer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(m_errorScreenLayer, 0, LV_PART_MAIN);

    const TextBoxWidgets errorTextBoxWidgets = createTextBoxWidgets(m_errorScreenLayer, errorBoxSpec);
    lv_obj_align(errorTextBoxWidgets.label, LV_ALIGN_TOP_LEFT, 0, 0);
  }

  void clear(Color screenBackgroundColor) override {
    throwIfNotInitialized();
    m_textBoxes.clear();
    if (m_errorScreenLayer != nullptr) {
      lv_obj_delete(m_errorScreenLayer);
      m_errorScreenLayer = nullptr;
    }
    lv_obj_t *activeScreen = lv_screen_active();
    lv_obj_clean(activeScreen);
    lv_obj_set_style_bg_color(activeScreen, toLvglColor(screenBackgroundColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(activeScreen, LV_OPA_COVER, LV_PART_MAIN);
  }

  std::uint32_t processEventsAndGetWaitMilliseconds() override {
    throwIfNotInitialized();
    return lv_timer_handler();
  }

private:
  TextBoxWidgets createTextBoxWidgets(lv_obj_t *container, const TextBoxSpec &textBoxSpec) {
    validateBounds(textBoxSpec.bounds);

    lv_obj_t *textBoxObject = lv_obj_create(container);
    lv_obj_set_pos(textBoxObject, textBoxSpec.bounds.x, textBoxSpec.bounds.y);
    lv_obj_set_size(textBoxObject, textBoxSpec.bounds.width, textBoxSpec.bounds.height);
    lv_obj_remove_flag(textBoxObject, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(textBoxObject, toLvglColor(textBoxSpec.backgroundColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(textBoxObject, textBoxSpec.backgroundOpacity, LV_PART_MAIN);
    lv_obj_set_style_border_color(textBoxObject, toLvglColor(textBoxSpec.borderColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(textBoxObject, textBoxSpec.borderWidth, LV_PART_MAIN);
    lv_obj_set_style_radius(textBoxObject, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(textBoxObject, 12, LV_PART_MAIN);

    lv_obj_t *textLabelObject = lv_label_create(textBoxObject);
    lv_label_set_text(textLabelObject, textBoxSpec.text.c_str());
    lv_label_set_long_mode(textLabelObject, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(textLabelObject, LV_PCT(100));
    lv_obj_set_style_text_color(textLabelObject, toLvglColor(textBoxSpec.textColor), LV_PART_MAIN);
    lv_obj_set_style_text_font(textLabelObject, fontForSize(textBoxSpec.fontSize), LV_PART_MAIN);

    return {textBoxObject, textLabelObject};
  }

  void throwIfNotInitialized() const {
    if (!m_isInitialized || m_lvglDisplay == nullptr) {
      throw std::logic_error("LVGL render backend is not initialized");
    }
  }

  bool                                              m_isInitialized{false};
  std::unique_ptr<internal::ILvglFramebufferDriver> m_framebufferDriver;
  lv_display_t                                     *m_lvglDisplay{nullptr};
  lv_obj_t                                         *m_errorScreenLayer{nullptr};
  std::unordered_map<WidgetId, TextBoxWidgets>      m_textBoxes;
};

} // namespace

namespace {

class LvglLinuxFramebufferDriver final : public internal::ILvglFramebufferDriver {
public:
  lv_display_t *createDisplay() override {
    return lv_linux_fbdev_create();
  }

  bool openFramebuffer(lv_display_t *lvglDisplay, const char *framebufferDevicePath) override {
    return lv_linux_fbdev_set_file(lvglDisplay, framebufferDevicePath) == LV_RESULT_OK;
  }
};

} // namespace

std::unique_ptr<IRenderBackend> makeLvglFramebufferRenderBackend() {
  return internal::makeLvglFramebufferRenderBackend(std::make_unique<LvglLinuxFramebufferDriver>());
}

namespace internal {

std::unique_ptr<IRenderBackend>
makeLvglFramebufferRenderBackend(std::unique_ptr<ILvglFramebufferDriver> framebufferDriver) {
  return std::make_unique<LvglFramebufferRenderBackend>(std::move(framebufferDriver));
}

} // namespace internal
} // namespace ui
} // namespace iot
