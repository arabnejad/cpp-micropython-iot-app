#include "display_cpp_bridge.h"

#include "iot/python/micropython_application_context.h"
#include "iot/ui/screen_manager.h"
#include "iot/ui/ui_types.h"

#include <exception>
#include <stdexcept>
#include <string>

namespace {

thread_local std::string latestErrorMessage;

iot_native_result_t success() noexcept {
  return {1, nullptr};
}

iot_native_result_t failure(const char *message) noexcept {
  latestErrorMessage = message;
  return {0, latestErrorMessage.c_str()};
}

iot::python::MicroPythonApplicationContext &context() {
  auto *activeContext = iot::python::MicroPythonApplicationContext::active();
  if (activeContext == nullptr) {
    throw std::logic_error("Python display module is not connected to the application runtime");
  }
  return *activeContext;
}

/*
 * Runs a C++ operation without letting its exception cross into MicroPython's
 * C code.
 *
 * FunctionToRun is the compiler-generated type of the lambda passed here. For
 * example:
 *
 *   return runSafely([=] {
 *     context().screenManager().clear({red, green, blue});
 *   });
 *
 * functionToRun() executes the lambda body. Using a template avoids wrapping
 * every bridge call in std::function.
 */
template <typename FunctionToRun> iot_native_result_t runSafely(FunctionToRun functionToRun) noexcept {
  try {
    functionToRun();
    return success();
  } catch (const std::exception &error) {
    return failure(error.what());
  } catch (...) {
    return failure("Unknown C++ display error");
  }
}

bool readOptionalUnsignedValue(int32_t suppliedValue, int32_t minimum, int32_t maximum, const char *name,
                               uint32_t &parsedUnsignedValue) {
  if (suppliedValue == -1) {
    return false;
  }
  if (suppliedValue < minimum || suppliedValue > maximum) {
    throw std::invalid_argument(std::string{name} + " is outside its supported range");
  }
  parsedUnsignedValue = static_cast<uint32_t>(suppliedValue);
  return true;
}

void applyOptionalColor(const iot_optional_color_t &suppliedColor, iot::ui::Color &destination) {
  if (suppliedColor.provided) {
    destination = {suppliedColor.red, suppliedColor.green, suppliedColor.blue};
  }
}

void applyOptionalByteValue(int32_t suppliedValue, uint8_t &destination, const char *name) {
  uint32_t parsedUnsignedValue = 0U;
  if (readOptionalUnsignedValue(suppliedValue, 0, UINT8_MAX, name, parsedUnsignedValue)) {
    destination = static_cast<uint8_t>(parsedUnsignedValue);
  }
}

void applyOptionalUint16Value(int32_t suppliedValue, int32_t minimum, uint16_t &destination, const char *name) {
  uint32_t parsedUnsignedValue = 0U;
  if (readOptionalUnsignedValue(suppliedValue, minimum, UINT16_MAX, name, parsedUnsignedValue)) {
    destination = static_cast<uint16_t>(parsedUnsignedValue);
  }
}

} // namespace

extern "C" iot_native_result_t iot_display_clear(uint8_t red, uint8_t green, uint8_t blue) {
  return runSafely([=] { context().screenManager().clear({red, green, blue}); });
}

extern "C" iot_native_result_t iot_display_draw_text_box(int32_t x, int32_t y, int32_t width, int32_t height,
                                                         const char *text, const iot_text_box_options_t *textBoxOptions,
                                                         uint64_t *widget_id) {
  return runSafely([=] {
    if (text == nullptr || textBoxOptions == nullptr || widget_id == nullptr) {
      throw std::invalid_argument("Text-box text, options, and widget ID output are required");
    }

    // TextBoxSpec owns the defaults. Python only replaces values that its
    // application supplied explicitly.
    iot::ui::TextBoxSpec textBoxSpec;
    textBoxSpec.bounds = {x, y, width, height};
    textBoxSpec.text   = text;

    applyOptionalColor(textBoxOptions->text_color, textBoxSpec.textColor);
    applyOptionalColor(textBoxOptions->background_color, textBoxSpec.backgroundColor);
    applyOptionalColor(textBoxOptions->border_color, textBoxSpec.borderColor);
    applyOptionalByteValue(textBoxOptions->background_opacity, textBoxSpec.backgroundOpacity, "background_opacity");
    applyOptionalUint16Value(textBoxOptions->border_width, 0, textBoxSpec.borderWidth, "border_width");
    applyOptionalUint16Value(textBoxOptions->font_size, 1, textBoxSpec.fontSize, "font_size");

    *widget_id = context().screenManager().drawTextBox(textBoxSpec);
  });
}

extern "C" iot_native_result_t iot_display_update_text_box(uint64_t widget_id, const char *text) {
  return runSafely([=] {
    if (widget_id == 0U || text == nullptr) {
      throw std::invalid_argument("Text-box widget ID and text are required");
    }
    context().screenManager().updateTextBox(widget_id, text);
  });
}

extern "C" iot_native_result_t iot_display_move_text_box(uint64_t widget_id, int32_t x, int32_t y) {
  return runSafely([=] {
    if (widget_id == 0U) {
      throw std::invalid_argument("Text-box widget ID is required");
    }
    context().screenManager().moveTextBox(widget_id, x, y);
  });
}

extern "C" iot_native_result_t iot_display_delete_text_box(uint64_t widget_id) {
  return runSafely([=] {
    if (widget_id == 0U) {
      throw std::invalid_argument("Text-box widget ID is required");
    }
    context().screenManager().deleteTextBox(widget_id);
  });
}

extern "C" iot_native_result_t iot_display_fill_area(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t red,
                                                     uint8_t green, uint8_t blue) {
  return runSafely([=] {
    iot::ui::FilledAreaSpec filledAreaSpec;
    filledAreaSpec.bounds = {x, y, width, height};
    filledAreaSpec.color  = {red, green, blue};
    context().screenManager().fillArea(filledAreaSpec);
  });
}

extern "C" iot_native_result_t iot_display_size(uint32_t *width, uint32_t *height) {
  return runSafely([=] {
    if (width == nullptr || height == nullptr) {
      throw std::invalid_argument("Display size output is missing");
    }
    *width  = context().displayWidth();
    *height = context().displayHeight();
  });
}

extern "C" iot_native_result_t iot_display_information(iot_display_information_t *displayInformation) {
  return runSafely([=] {
    if (displayInformation == nullptr) {
      throw std::invalid_argument("Display-information output is missing");
    }

    const auto &activeDisplay                   = context().activeDisplay();
    displayInformation->connected_display_count = context().connectedDisplays().size();
    displayInformation->connector_name          = activeDisplay.display().displayId.connectorName.c_str();
    displayInformation->manufacturer            = activeDisplay.display().manufacturer.c_str();
    displayInformation->model                   = activeDisplay.display().model.c_str();
    displayInformation->width                   = activeDisplay.mode().width;
    displayInformation->height                  = activeDisplay.mode().height;
    displayInformation->refresh_rate_hz         = activeDisplay.mode().refreshRateHz;
  });
}
