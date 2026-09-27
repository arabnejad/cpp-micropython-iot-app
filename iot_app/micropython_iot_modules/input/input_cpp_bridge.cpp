#include "input_cpp_bridge.h"

#include "native_bridge_error_handler.h"
#include "iot/input/adafruit_mini_i2c_gamepad.h"
#include "iot/input/seengreat_oled_hat.h"

#include <stdexcept>

namespace {

/* Controller failures become RuntimeError messages in mod_iot_input.c. */
thread_local iot::python::internal::NativeBridgeErrorHandler nativeBridgeErrorHandler{"Unknown C++ controller error"};

/*
 * The MicroPython module is C, so it keeps the C++ controller as a void *.
 * It points to an object made by iot_adafruit_controller_create() or
 * iot_seengreat_controller_create(); this function does not create another one.
 * Check for a closed controller, then return a reference to that same object.
 */
iot::input::InputControllerBase &inputControllerFromHandle(void *handle) {
  if (handle == nullptr) {
    throw std::logic_error("The controller is closed");
  }
  return *static_cast<iot::input::InputControllerBase *>(handle);
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

extern "C" iot_native_pointer_result_t iot_adafruit_controller_create(void) {
  void      *createdAdafruitController = nullptr;
  const auto creationResult =
      nativeBridgeErrorHandler.runSafely([&] { createdAdafruitController = new iot::input::AdafruitMiniI2cGamepad(); });
  return {createdAdafruitController, creationResult.error_message};
}

extern "C" iot_native_pointer_result_t iot_seengreat_controller_create(void) {
  void      *createdSeenGreatController = nullptr;
  const auto creationResult             = nativeBridgeErrorHandler.runSafely(
      [&] { createdSeenGreatController = new iot::input::SeenGreatOledHatController(); });
  return {createdSeenGreatController, creationResult.error_message};
}

extern "C" void iot_controller_destroy(void *controller_handle) {
  delete static_cast<iot::input::InputControllerBase *>(controller_handle);
}

extern "C" iot_native_result_t iot_controller_model_name(void *controller_handle, const char **model_name) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (model_name == nullptr) {
      throw std::invalid_argument("Controller model-name output is missing");
    }
    *model_name = inputControllerFromHandle(controller_handle).modelName();
  });
}

extern "C" iot_native_result_t iot_controller_board_type(void *controller_handle, const char **board_type) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (board_type == nullptr) {
      throw std::invalid_argument("Controller board-type output is missing");
    }
    *board_type = inputControllerFromHandle(controller_handle).boardType();
  });
}

extern "C" iot_native_result_t iot_controller_connect(void *controller_handle) {
  return nativeBridgeErrorHandler.runSafely([=] { inputControllerFromHandle(controller_handle).connect(); });
}

extern "C" iot_native_result_t iot_adafruit_controller_calibrate_joystick(void  *controller_handle,
                                                                          size_t number_of_samples, int dead_zone) {
  return nativeBridgeErrorHandler.runSafely([=] {
    auto *adafruit = dynamic_cast<iot::input::AdafruitMiniI2cGamepad *>(&inputControllerFromHandle(controller_handle));
    if (!adafruit)
      throw std::logic_error("Joystick calibration is only available for the Adafruit gamepad");
    adafruit->calibrateJoystick(number_of_samples, dead_zone);
  });
}

extern "C" iot_native_result_t iot_controller_refresh_input_state(void *controller_handle) {
  return nativeBridgeErrorHandler.runSafely([=] { inputControllerFromHandle(controller_handle).refreshInputState(); });
}

extern "C" iot_native_result_t iot_controller_is_connected(void *controller_handle, int *is_connected) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (is_connected == nullptr) {
      throw std::invalid_argument("Controller connection-state output is missing");
    }
    *is_connected = inputControllerFromHandle(controller_handle).isConnected() ? 1 : 0;
  });
}

extern "C" iot_native_result_t iot_controller_read_state(void *controller_handle, iot_controller_state_t *state) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (state == nullptr) {
      throw std::invalid_argument("Controller state output is missing");
    }

    auto      &controller = inputControllerFromHandle(controller_handle);
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

extern "C" iot_native_result_t iot_controller_joystick_direction(void *controller_handle, const char **direction) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (direction == nullptr) {
      throw std::invalid_argument("Controller joystick-direction output is missing");
    }
    *direction = joystickDirectionName(inputControllerFromHandle(controller_handle).joystick().direction());
  });
}

extern "C" iot_native_result_t iot_adafruit_controller_read_connection_information(
    void *controller_handle, iot_adafruit_controller_connection_information_t *connection_information) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (connection_information == nullptr) {
      throw std::invalid_argument("Adafruit I2C connection-information output is missing");
    }

    const auto *adafruit =
        dynamic_cast<const iot::input::AdafruitMiniI2cGamepad *>(&inputControllerFromHandle(controller_handle));
    if (!adafruit)
      throw std::logic_error("I2C connection information is only available for the Adafruit gamepad");
    connection_information->bus_number  = adafruit->i2cBusNumber();
    connection_information->address     = adafruit->i2cAddress();
    connection_information->device_path = adafruit->i2cDevicePath().c_str();
  });
}

extern "C" iot_native_result_t
iot_adafruit_controller_read_diagnostics(void                                         *controller_handle,
                                         iot_adafruit_controller_device_information_t *diagnostics) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (diagnostics == nullptr) {
      throw std::invalid_argument("Adafruit diagnostics output is missing");
    }

    const auto *adafruit =
        dynamic_cast<const iot::input::AdafruitMiniI2cGamepad *>(&inputControllerFromHandle(controller_handle));
    if (!adafruit)
      throw std::logic_error("Firmware diagnostics are only available for the Adafruit gamepad");
    diagnostics->processor_hardware_id                      = adafruit->processorHardwareId();
    diagnostics->combined_product_id_and_firmware_date_code = adafruit->productIdAndFirmwareDateCode();
    diagnostics->firmware_product_id                        = adafruit->firmwareProductId();
    diagnostics->firmware_date_code                         = adafruit->firmwareDateCode();
  });
}
