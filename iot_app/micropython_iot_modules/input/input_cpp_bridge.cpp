#include "input_cpp_bridge.h"

#include "native_bridge_error_handler.h"
#include "iot/input/adafruit_mini_i2c_gamepad.h"

#include <stdexcept>

namespace {

/* Gamepad failures become RuntimeError messages in mod_iot_input.c. */
thread_local iot::python::internal::NativeBridgeErrorHandler nativeBridgeErrorHandler{"Unknown C++ gamepad error"};

iot::input::AdafruitMiniI2cGamepad &gamepad(void *handle) {
  if (handle == nullptr) {
    throw std::logic_error("The gamepad is closed");
  }
  return *static_cast<iot::input::AdafruitMiniI2cGamepad *>(handle);
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
  void      *createdGamepad = nullptr;
  const auto creationResult = nativeBridgeErrorHandler.runSafely(
      [&] { createdGamepad = new iot::input::AdafruitMiniI2cGamepad(i2c_bus_number, i2c_address); });
  return {createdGamepad, creationResult.error_message};
}

extern "C" void iot_gamepad_destroy(void *gamepad_handle) {
  delete static_cast<iot::input::AdafruitMiniI2cGamepad *>(gamepad_handle);
}

extern "C" iot_native_result_t iot_gamepad_model_name(void *gamepad_handle, const char **model_name) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (model_name == nullptr) {
      throw std::invalid_argument("Gamepad model-name output is missing");
    }
    *model_name = gamepad(gamepad_handle).modelName();
  });
}

extern "C" iot_native_result_t iot_gamepad_connect(void *gamepad_handle) {
  return nativeBridgeErrorHandler.runSafely([=] { gamepad(gamepad_handle).connect(); });
}

extern "C" iot_native_result_t iot_gamepad_calibrate_joystick(void *gamepad_handle, size_t number_of_samples,
                                                              int dead_zone) {
  return nativeBridgeErrorHandler.runSafely(
      [=] { gamepad(gamepad_handle).calibrateJoystick(number_of_samples, dead_zone); });
}

extern "C" iot_native_result_t iot_gamepad_refresh_input_state(void *gamepad_handle) {
  return nativeBridgeErrorHandler.runSafely([=] { gamepad(gamepad_handle).refreshInputState(); });
}

extern "C" iot_native_result_t iot_gamepad_is_connected(void *gamepad_handle, int *is_connected) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (is_connected == nullptr) {
      throw std::invalid_argument("Gamepad connection-state output is missing");
    }
    *is_connected = gamepad(gamepad_handle).isConnected() ? 1 : 0;
  });
}

extern "C" iot_native_result_t iot_gamepad_read_state(void *gamepad_handle, iot_gamepad_state_t *state) {
  return nativeBridgeErrorHandler.runSafely([=] {
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
  return nativeBridgeErrorHandler.runSafely([=] {
    if (direction == nullptr) {
      throw std::invalid_argument("Gamepad joystick-direction output is missing");
    }
    *direction = joystickDirectionName(gamepad(gamepad_handle).joystick().direction());
  });
}

extern "C" iot_native_result_t
iot_gamepad_read_connection_information(void                                 *gamepad_handle,
                                        iot_gamepad_connection_information_t *connection_information) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (connection_information == nullptr) {
      throw std::invalid_argument("Gamepad connection-information output is missing");
    }

    const auto &controller              = gamepad(gamepad_handle);
    connection_information->bus_number  = controller.i2cBusNumber();
    connection_information->address     = controller.i2cAddress();
    connection_information->device_path = controller.i2cDevicePath().c_str();
  });
}

extern "C" iot_native_result_t iot_gamepad_read_diagnostics(void                             *gamepad_handle,
                                                            iot_gamepad_device_information_t *diagnostics) {
  return nativeBridgeErrorHandler.runSafely([=] {
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
