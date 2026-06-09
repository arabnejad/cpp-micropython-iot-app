#include "input_cpp_bridge.h"

#include "iot/input/adafruit_mini_i2c_gamepad.h"

#include <exception>
#include <new>
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

iot::input::AdafruitMiniI2cGamepad &gamepad(void *handle) {
  if (handle == nullptr) {
    throw std::logic_error("The gamepad is closed");
  }
  return *static_cast<iot::input::AdafruitMiniI2cGamepad *>(handle);
}

/*
 * Runs a C++ operation without letting its exception cross into MicroPython's
 * C code.
 *
 * FunctionToRun is the compiler-generated type of the lambda passed here. For
 * example:
 *
 *   return runSafely([=] {
 *     gamepad(gamepad_handle).connect();
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
    return failure("Unknown C++ gamepad error");
  }
}

/* Converts a C++ direction into the text returned to Python. */
const char *joystickDirectionName(iot::input::JoystickDirection direction) noexcept {
  switch (direction) {
  case iot::input::JoystickDirection::Center:
    return "center";
  case iot::input::JoystickDirection::Left:
    return "left";
  case iot::input::JoystickDirection::Right:
    return "right";
  case iot::input::JoystickDirection::Up:
    return "up";
  case iot::input::JoystickDirection::Down:
    return "down";
  case iot::input::JoystickDirection::UpLeft:
    return "up_left";
  case iot::input::JoystickDirection::UpRight:
    return "up_right";
  case iot::input::JoystickDirection::DownLeft:
    return "down_left";
  case iot::input::JoystickDirection::DownRight:
    return "down_right";
  }
  return "center";
}

} // namespace

extern "C" iot_native_pointer_result_t iot_gamepad_create(int i2c_bus_number, uint8_t i2c_address) {
  try {
    return {new iot::input::AdafruitMiniI2cGamepad(i2c_bus_number, i2c_address), nullptr};
  } catch (const std::exception &error) {
    latestErrorMessage = error.what();
  } catch (...) {
    latestErrorMessage = "Unknown C++ gamepad construction error";
  }
  return {nullptr, latestErrorMessage.c_str()};
}

extern "C" void iot_gamepad_destroy(void *gamepad_handle) {
  delete static_cast<iot::input::AdafruitMiniI2cGamepad *>(gamepad_handle);
}

extern "C" iot_native_result_t iot_gamepad_model_name(void *gamepad_handle, const char **model_name) {
  return runSafely([=] {
    if (model_name == nullptr) {
      throw std::invalid_argument("Gamepad model-name output is missing");
    }
    *model_name = gamepad(gamepad_handle).modelName();
  });
}

extern "C" iot_native_result_t iot_gamepad_connect(void *gamepad_handle) {
  return runSafely([=] { gamepad(gamepad_handle).connect(); });
}

extern "C" iot_native_result_t iot_gamepad_calibrate_joystick(void *gamepad_handle, size_t number_of_samples,
                                                              int dead_zone) {
  return runSafely([=] { gamepad(gamepad_handle).calibrateJoystick(number_of_samples, dead_zone); });
}

extern "C" iot_native_result_t iot_gamepad_refresh_input_state(void *gamepad_handle) {
  return runSafely([=] { gamepad(gamepad_handle).refreshInputState(); });
}

extern "C" iot_native_result_t iot_gamepad_is_connected(void *gamepad_handle, int *is_connected) {
  return runSafely([=] {
    if (is_connected == nullptr) {
      throw std::invalid_argument("Gamepad connection-state output is missing");
    }
    *is_connected = gamepad(gamepad_handle).isConnected() ? 1 : 0;
  });
}

extern "C" iot_native_result_t iot_gamepad_read_state(void *gamepad_handle, iot_gamepad_state_t *state) {
  return runSafely([=] {
    if (state == nullptr) {
      throw std::invalid_argument("Gamepad state output is missing");
    }

    auto      &controller = gamepad(gamepad_handle);
    const auto position   = controller.joystick().position();
    const auto centre     = controller.joystick().centre();

    uint32_t pressedButtonsMask = 0U;
    for (const auto button : controller.buttons().pressed()) {
      pressedButtonsMask |= static_cast<std::uint32_t>(button);
    }

    state->x                    = position.x;
    state->y                    = position.y;
    state->centre_x             = centre.x;
    state->centre_y             = centre.y;
    state->dead_zone            = controller.joystick().deadZone();
    state->pressed_buttons_mask = pressedButtonsMask;
  });
}

extern "C" iot_native_result_t iot_gamepad_joystick_direction(void *gamepad_handle, const char **direction) {
  return runSafely([=] {
    if (direction == nullptr) {
      throw std::invalid_argument("Gamepad joystick-direction output is missing");
    }
    *direction = joystickDirectionName(gamepad(gamepad_handle).joystick().direction());
  });
}

extern "C" iot_native_result_t iot_gamepad_read_diagnostics(void                             *gamepad_handle,
                                                            iot_gamepad_device_information_t *diagnostics) {
  return runSafely([=] {
    if (diagnostics == nullptr) {
      throw std::invalid_argument("Gamepad diagnostics output is missing");
    }

    const auto &controller                                  = gamepad(gamepad_handle);
    diagnostics->processor_hardware_id                      = controller.processorHardwareId();
    diagnostics->combined_product_id_and_firmware_date_code = controller.productIdAndFirmwareDateCode();
    diagnostics->firmware_product_id                        = controller.firmwareProductId();
    diagnostics->firmware_date_code                         = controller.firmwareDateCode();
  });
}
