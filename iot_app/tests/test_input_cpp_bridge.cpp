#include "input_cpp_bridge.h"

#include "simulated_gamepad_i2c_board.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace {

TEST(InputCppBridgeTest, ConvertsInvalidGamepadConstructionIntoAnErrorResult) {
  const iot_native_pointer_result_t gamepadCreationResult = iot_gamepad_create(256, 0x50U);

  EXPECT_EQ(gamepadCreationResult.value, nullptr);
  ASSERT_NE(gamepadCreationResult.error_message, nullptr);
  EXPECT_NE(std::string(gamepadCreationResult.error_message).find("bus number"), std::string::npos);
}

TEST(InputCppBridgeTest, RejectsEveryOperationWhenTheGamepadHandleIsNull) {
  const char                          *gamepadModelName   = nullptr;
  const char                          *joystickDirection  = nullptr;
  int                                  gamepadIsConnected = 0;
  iot_gamepad_state_t                  gamepadState{};
  iot_gamepad_connection_information_t gamepadConnectionInformation{};
  iot_gamepad_device_information_t     gamepadDeviceInformation{};

  EXPECT_FALSE(iot_gamepad_model_name(nullptr, &gamepadModelName).succeeded);
  EXPECT_FALSE(iot_gamepad_connect(nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_calibrate_joystick(nullptr, 1U, 100).succeeded);
  EXPECT_FALSE(iot_gamepad_refresh_input_state(nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_is_connected(nullptr, &gamepadIsConnected).succeeded);
  EXPECT_FALSE(iot_gamepad_read_state(nullptr, &gamepadState).succeeded);
  EXPECT_FALSE(iot_gamepad_joystick_direction(nullptr, &joystickDirection).succeeded);
  EXPECT_FALSE(iot_gamepad_read_connection_information(nullptr, &gamepadConnectionInformation).succeeded);
  EXPECT_FALSE(iot_gamepad_read_diagnostics(nullptr, &gamepadDeviceInformation).succeeded);
}

TEST(InputCppBridgeTest, ChecksEveryRequiredOutputPointer) {
  iot::tests::SimulatedGamepadI2cBoard    simulatedGamepadBoard;
  iot::tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);
  const iot_native_pointer_result_t       gamepadCreationResult = iot_gamepad_create(1, 0x50U);
  ASSERT_NE(gamepadCreationResult.value, nullptr);

  EXPECT_FALSE(iot_gamepad_model_name(gamepadCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_is_connected(gamepadCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_read_state(gamepadCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_joystick_direction(gamepadCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_read_connection_information(gamepadCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_gamepad_read_diagnostics(gamepadCreationResult.value, nullptr).succeeded);
  iot_gamepad_destroy(gamepadCreationResult.value);
}

TEST(InputCppBridgeTest, SafelyIgnoresDestructionOfANullGamepadHandle) {
  EXPECT_NO_THROW(iot_gamepad_destroy(nullptr));
}

TEST(InputCppBridgeTest, UsesEveryBridgeOperationWithARealDriverAndASimulatedGamepad) {
  iot::tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies(0xffffffdfU, 512U, 513U);
  iot::tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);

  const iot_native_pointer_result_t gamepadCreationResult = iot_gamepad_create(1, 0x50U);
  ASSERT_NE(gamepadCreationResult.value, nullptr);
  const char *gamepadModelName = nullptr;
  ASSERT_TRUE(iot_gamepad_model_name(gamepadCreationResult.value, &gamepadModelName).succeeded);
  EXPECT_STREQ(gamepadModelName, "Adafruit Mini I2C STEMMA QT Gamepad");

  iot_gamepad_connection_information_t gamepadConnectionInformation{};
  ASSERT_TRUE(
      iot_gamepad_read_connection_information(gamepadCreationResult.value, &gamepadConnectionInformation).succeeded);
  EXPECT_EQ(gamepadConnectionInformation.bus_number, 1);
  EXPECT_EQ(gamepadConnectionInformation.address, 0x50U);
  EXPECT_STREQ(gamepadConnectionInformation.device_path, "/dev/i2c-1");

  ASSERT_TRUE(iot_gamepad_connect(gamepadCreationResult.value).succeeded);

  int gamepadIsConnected = 0;
  ASSERT_TRUE(iot_gamepad_is_connected(gamepadCreationResult.value, &gamepadIsConnected).succeeded);
  EXPECT_EQ(gamepadIsConnected, 1);

  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  ASSERT_TRUE(iot_gamepad_calibrate_joystick(gamepadCreationResult.value, 1U, 20).succeeded);

  simulatedGamepadBoard.addReply({0xffU, 0xffU, 0xffU, 0xdfU});
  simulatedGamepadBoard.addReply({0x00U, 0x64U});
  simulatedGamepadBoard.addReply({0x03U, 0x84U});
  ASSERT_TRUE(iot_gamepad_refresh_input_state(gamepadCreationResult.value).succeeded);

  iot_gamepad_state_t gamepadState{};
  ASSERT_TRUE(iot_gamepad_read_state(gamepadCreationResult.value, &gamepadState).succeeded);
  EXPECT_EQ(gamepadState.x, 923);
  EXPECT_EQ(gamepadState.y, 123);
  EXPECT_EQ(gamepadState.pressed_buttons_mask, 1U << 2U);

  const char *joystickDirection = nullptr;
  ASSERT_TRUE(iot_gamepad_joystick_direction(gamepadCreationResult.value, &joystickDirection).succeeded);
  EXPECT_STREQ(joystickDirection, "down_right");

  iot_gamepad_device_information_t gamepadDeviceInformation{};
  ASSERT_TRUE(iot_gamepad_read_diagnostics(gamepadCreationResult.value, &gamepadDeviceInformation).succeeded);
  EXPECT_EQ(gamepadDeviceInformation.processor_hardware_id, 0x55U);
  EXPECT_EQ(gamepadDeviceInformation.firmware_product_id, 5743U);
  EXPECT_EQ(gamepadDeviceInformation.firmware_date_code, 0x7a97U);
  iot_gamepad_destroy(gamepadCreationResult.value);
}

TEST(InputCppBridgeTest, ConvertsEveryButtonAndJoystickDirectionToThePublicCValues) {
  iot::tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies();
  iot::tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);
  const iot_native_pointer_result_t       gamepadCreationResult = iot_gamepad_create(1, 0x50U);
  ASSERT_NE(gamepadCreationResult.value, nullptr);
  ASSERT_TRUE(iot_gamepad_connect(gamepadCreationResult.value).succeeded);

  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  ASSERT_TRUE(iot_gamepad_calibrate_joystick(gamepadCreationResult.value, 1U, 20).succeeded);

  const std::array<std::pair<std::uint16_t, std::uint16_t>, 9U> rawJoystickAxisValues{{
      {512U, 512U},
      {1023U, 512U},
      {0U, 512U},
      {512U, 1023U},
      {512U, 0U},
      {0U, 1023U},
      {1023U, 1023U},
      {0U, 0U},
      {1023U, 0U},
  }};
  const std::array<const char *, 9U>                            expectedDirections{{
      "center",
      "left",
      "right",
      "down",
      "up",
      "down_right",
      "down_left",
      "up_right",
      "up_left",
  }};
  for (std::size_t directionIndex = 0; directionIndex < rawJoystickAxisValues.size(); ++directionIndex) {
    // Inputs 0, 1, 2, 5, 6, and 16 are active-low. Clear exactly those bits
    // so the driver reports all six public buttons as pressed.
    simulatedGamepadBoard.addReply({0xffU, 0xfeU, 0xffU, 0x98U});
    simulatedGamepadBoard.addReply({static_cast<std::uint8_t>(rawJoystickAxisValues[directionIndex].first >> 8U),
                                    static_cast<std::uint8_t>(rawJoystickAxisValues[directionIndex].first)});
    simulatedGamepadBoard.addReply({static_cast<std::uint8_t>(rawJoystickAxisValues[directionIndex].second >> 8U),
                                    static_cast<std::uint8_t>(rawJoystickAxisValues[directionIndex].second)});
    ASSERT_TRUE(iot_gamepad_refresh_input_state(gamepadCreationResult.value).succeeded);
    const char *joystickDirection = nullptr;
    ASSERT_TRUE(iot_gamepad_joystick_direction(gamepadCreationResult.value, &joystickDirection).succeeded);
    EXPECT_STREQ(joystickDirection, expectedDirections[directionIndex]);
  }

  iot_gamepad_state_t gamepadState{};
  ASSERT_TRUE(iot_gamepad_read_state(gamepadCreationResult.value, &gamepadState).succeeded);
  EXPECT_EQ(gamepadState.pressed_buttons_mask, 0x3fU);
  iot_gamepad_destroy(gamepadCreationResult.value);
}

} // namespace
