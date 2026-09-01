#include "iot/python/micropython_runtime.h"

#include "simulated_gamepad_i2c_board.h"

#include <gtest/gtest.h>

namespace iot {
namespace python {
namespace {

PythonApplication createInputModuleTestApplication(std::string sourceCode) {
  PythonApplication pythonApplication;
  pythonApplication.applicationId   = "input-module-test";
  pythonApplication.applicationName = "Input module test";
  pythonApplication.entryPointPath  = "main.py";
  pythonApplication.sourceCode      = std::move(sourceCode);
  return pythonApplication;
}

TEST(MicroPythonInputModuleTest, ValidatesGamepadConstructorArgumentsBeforeOpeningAnI2cDevice) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(
      createInputModuleTestApplication("import iot\n"
                                       "try:\n"
                                       "    iot.input.AdafruitMiniI2cGamepad(i2c_bus_number=1, i2c_address=128)\n"
                                       "    assert False\n"
                                       "except ValueError:\n"
                                       "    pass\n"
                                       "try:\n"
                                       "    iot.input.AdafruitMiniI2cGamepad(i2c_bus_number=256, i2c_address=0x50)\n"
                                       "    assert False\n"
                                       "except RuntimeError:\n"
                                       "    pass\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

TEST(MicroPythonInputModuleTest, ExposesTheGamepadClassThroughThePublicIotModule) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(
      createInputModuleTestApplication("import iot\n"
                                       "assert iot.input.AdafruitMiniI2cGamepad is not None\n"
                                       "try:\n"
                                       "    iot.input.AdafruitMiniI2cGamepad()\n"
                                       "    assert False\n"
                                       "except TypeError:\n"
                                       "    pass\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

TEST(MicroPythonInputModuleTest, LetsPythonUseEveryGamepadMethodWithASimulatedGamepad) {
  tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies(0xffffffdfU, 512U, 513U);
  tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);

  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult =
      microPythonRuntime.executeApplication(createInputModuleTestApplication(
          "import iot\n"
          "gamepad = iot.input.AdafruitMiniI2cGamepad(i2c_bus_number=1, i2c_address=0x50)\n"
          "assert gamepad.model_name() == 'Adafruit Mini I2C STEMMA QT Gamepad'\n"
          "assert gamepad.connection_information() == {\n"
          "    'bus_number': 1,\n"
          "    'address': 0x50,\n"
          "    'device_path': '/dev/i2c-1',\n"
          "}\n"
          "assert gamepad.is_connected() is False\n"
          "gamepad.connect()\n"
          "assert gamepad.is_connected() is True\n"
          "joystick = gamepad.joystick()\n"
          "gamepad_buttons = gamepad.buttons()\n"
          "assert joystick.position() == (511, 510)\n"
          "assert joystick.centre() == (512, 512)\n"
          "assert joystick.dead_zone() == 100\n"
          "assert joystick.direction() == 'center'\n"
          "assert gamepad_buttons.pressed() == ('A',)\n"
          "assert gamepad_buttons.is_pressed('A') is True\n"
          "assert gamepad_buttons.is_pressed('B') is False\n"
          "assert gamepad.processor_hardware_id() == 0x55\n"
          "assert gamepad.firmware_product_id() == 5743\n"
          "assert gamepad.firmware_date_code() == 0x7a97\n"
          "assert gamepad.combined_product_id_and_firmware_date_code() == 0x166f7a97\n"
          "gamepad.close()\n"
          "try:\n"
          "    gamepad.is_connected()\n"
          "    assert False\n"
          "except ValueError:\n"
          "    pass\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

TEST(MicroPythonInputModuleTest, ReportsErrorsForInvalidCalibrationUnknownButtonsAndViewsUsedAfterClose) {
  tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies();
  tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);

  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(
      createInputModuleTestApplication("import iot\n"
                                       "gamepad = iot.input.AdafruitMiniI2cGamepad(1, 0x50)\n"
                                       "gamepad.connect()\n"
                                       "gamepad_buttons = gamepad.buttons()\n"
                                       "joystick = gamepad.joystick()\n"
                                       "try:\n"
                                       "    gamepad.calibrate_joystick(number_of_samples=0)\n"
                                       "    assert False\n"
                                       "except ValueError:\n"
                                       "    pass\n"
                                       "try:\n"
                                       "    gamepad.calibrate_joystick(dead_zone=1 << 100)\n"
                                       "    assert False\n"
                                       "except OverflowError:\n"
                                       "    pass\n"
                                       "try:\n"
                                       "    gamepad_buttons.is_pressed('NotAButton')\n"
                                       "    assert False\n"
                                       "except ValueError:\n"
                                       "    pass\n"
                                       "gamepad.close()\n"
                                       "for operation in (gamepad_buttons.pressed, joystick.position):\n"
                                       "    try:\n"
                                       "        operation()\n"
                                       "        assert False\n"
                                       "    except ValueError:\n"
                                       "        pass\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

TEST(MicroPythonInputModuleTest, RefreshesTheStateAndChecksEveryPublicButtonName) {
  tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies();
  // One calibration sample, followed by one complete refresh. All six button
  // inputs are low in the refresh reply, which means all buttons are pressed.
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0xffU, 0xfeU, 0xffU, 0x98U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);

  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(
      createInputModuleTestApplication("import iot\n"
                                       "gamepad = iot.input.AdafruitMiniI2cGamepad(1, 0x50)\n"
                                       "gamepad.connect()\n"
                                       "gamepad.calibrate_joystick(number_of_samples=1, dead_zone=20)\n"
                                       "gamepad.refresh_input_state()\n"
                                       "gamepad_buttons = gamepad.buttons()\n"
                                       "assert gamepad_buttons.pressed() == ('X', 'Y', 'A', 'B', 'Select', 'Start')\n"
                                       "for button_name in ('X', 'Y', 'A', 'B', 'Select', 'Start'):\n"
                                       "    assert gamepad_buttons.is_pressed(button_name) is True\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

} // namespace
} // namespace python
} // namespace iot
