#include "input_cpp_bridge.h"

#include "simulated_gamepad_i2c_board.h"

#include <gtest/gtest.h>

#include <array>

namespace {

TEST(InputCppBridgeTest, RejectsEveryOperationWhenTheControllerHandleIsNull) {
  const char                                      *controllerModelName   = nullptr;
  const char                                      *controllerBoardType   = nullptr;
  const char                                      *joystickDirection     = nullptr;
  int                                              controllerIsConnected = 0;
  iot_controller_state_t                           controllerState{};
  iot_adafruit_controller_connection_information_t adafruitConnectionInformation{};
  iot_adafruit_controller_device_information_t     adafruitDeviceInformation{};

  EXPECT_FALSE(iot_controller_model_name(nullptr, &controllerModelName).succeeded);
  EXPECT_FALSE(iot_controller_board_type(nullptr, &controllerBoardType).succeeded);
  EXPECT_FALSE(iot_controller_connect(nullptr).succeeded);
  EXPECT_FALSE(iot_adafruit_controller_calibrate_joystick(nullptr, 1U, 100).succeeded);
  EXPECT_FALSE(iot_controller_refresh_input_state(nullptr).succeeded);
  EXPECT_FALSE(iot_controller_is_connected(nullptr, &controllerIsConnected).succeeded);
  EXPECT_FALSE(iot_controller_read_state(nullptr, &controllerState).succeeded);
  EXPECT_FALSE(iot_controller_joystick_direction(nullptr, &joystickDirection).succeeded);
  EXPECT_FALSE(iot_adafruit_controller_read_connection_information(nullptr, &adafruitConnectionInformation).succeeded);
  EXPECT_FALSE(iot_adafruit_controller_read_diagnostics(nullptr, &adafruitDeviceInformation).succeeded);
}

TEST(InputCppBridgeTest, ChecksEveryRequiredOutputPointer) {
  iot::tests::SimulatedGamepadI2cBoard    simulatedGamepadBoard;
  iot::tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);
  const iot_native_pointer_result_t       adafruitControllerCreationResult = iot_adafruit_controller_create();
  ASSERT_NE(adafruitControllerCreationResult.value, nullptr);

  EXPECT_FALSE(iot_controller_model_name(adafruitControllerCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_controller_board_type(adafruitControllerCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_controller_is_connected(adafruitControllerCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_controller_read_state(adafruitControllerCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_controller_joystick_direction(adafruitControllerCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(
      iot_adafruit_controller_read_connection_information(adafruitControllerCreationResult.value, nullptr).succeeded);
  EXPECT_FALSE(iot_adafruit_controller_read_diagnostics(adafruitControllerCreationResult.value, nullptr).succeeded);
  iot_controller_destroy(adafruitControllerCreationResult.value);
}

TEST(InputCppBridgeTest, SafelyIgnoresDestructionOfANullControllerHandle) {
  EXPECT_NO_THROW(iot_controller_destroy(nullptr));
}

TEST(InputCppBridgeTest, UsesCommonAndAdafruitOperationsWithASimulatedBoard) {
  iot::tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies(0xffffffdfU, 512U, 513U);
  iot::tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);

  const iot_native_pointer_result_t adafruitControllerCreationResult = iot_adafruit_controller_create();
  ASSERT_NE(adafruitControllerCreationResult.value, nullptr);
  const char *controllerModelName = nullptr;
  ASSERT_TRUE(iot_controller_model_name(adafruitControllerCreationResult.value, &controllerModelName).succeeded);
  EXPECT_STREQ(controllerModelName, "Adafruit Mini I2C STEMMA QT Gamepad");
  const char *controllerBoardType = nullptr;
  ASSERT_TRUE(iot_controller_board_type(adafruitControllerCreationResult.value, &controllerBoardType).succeeded);
  EXPECT_STREQ(controllerBoardType, "adafruit_mini_i2c_gamepad");

  iot_adafruit_controller_connection_information_t adafruitConnectionInformation{};
  ASSERT_TRUE(iot_adafruit_controller_read_connection_information(adafruitControllerCreationResult.value,
                                                                  &adafruitConnectionInformation)
                  .succeeded);
  EXPECT_EQ(adafruitConnectionInformation.bus_number, 1);
  EXPECT_EQ(adafruitConnectionInformation.address, 0x50U);
  EXPECT_STREQ(adafruitConnectionInformation.device_path, "/dev/i2c-1");

  ASSERT_TRUE(iot_controller_connect(adafruitControllerCreationResult.value).succeeded);

  int controllerIsConnected = 0;
  ASSERT_TRUE(iot_controller_is_connected(adafruitControllerCreationResult.value, &controllerIsConnected).succeeded);
  EXPECT_EQ(controllerIsConnected, 1);

  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  ASSERT_TRUE(iot_adafruit_controller_calibrate_joystick(adafruitControllerCreationResult.value, 1U, 20).succeeded);

  simulatedGamepadBoard.addReply({0xffU, 0xffU, 0xffU, 0xdfU});
  simulatedGamepadBoard.addReply({0x00U, 0x64U});
  simulatedGamepadBoard.addReply({0x03U, 0x84U});
  ASSERT_TRUE(iot_controller_refresh_input_state(adafruitControllerCreationResult.value).succeeded);

  iot_controller_state_t controllerState{};
  ASSERT_TRUE(iot_controller_read_state(adafruitControllerCreationResult.value, &controllerState).succeeded);
  EXPECT_EQ(controllerState.x, 923);
  EXPECT_EQ(controllerState.y, 123);
  EXPECT_EQ(controllerState.pressed_buttons_mask, 1U << 2U);

  const char *joystickDirection = nullptr;
  ASSERT_TRUE(iot_controller_joystick_direction(adafruitControllerCreationResult.value, &joystickDirection).succeeded);
  EXPECT_STREQ(joystickDirection, "down_right");

  iot_adafruit_controller_device_information_t adafruitDeviceInformation{};
  ASSERT_TRUE(
      iot_adafruit_controller_read_diagnostics(adafruitControllerCreationResult.value, &adafruitDeviceInformation)
          .succeeded);
  EXPECT_EQ(adafruitDeviceInformation.processor_hardware_id, 0x55U);
  EXPECT_EQ(adafruitDeviceInformation.firmware_product_id, 5743U);
  EXPECT_EQ(adafruitDeviceInformation.firmware_date_code, 0x7a97U);
  iot_controller_destroy(adafruitControllerCreationResult.value);
}

TEST(InputCppBridgeTest, ConvertsAdafruitButtonsAndJoystickDirectionsToThePublicCValues) {
  iot::tests::SimulatedGamepadI2cBoard simulatedGamepadBoard;
  simulatedGamepadBoard.addSuccessfulConnectionReplies();
  iot::tests::UseSimulatedGamepadI2cBoard useSimulatedGamepadBoard(simulatedGamepadBoard);
  const iot_native_pointer_result_t       adafruitControllerCreationResult = iot_adafruit_controller_create();
  ASSERT_NE(adafruitControllerCreationResult.value, nullptr);
  ASSERT_TRUE(iot_controller_connect(adafruitControllerCreationResult.value).succeeded);

  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  simulatedGamepadBoard.addReply({0x02U, 0x00U});
  ASSERT_TRUE(iot_adafruit_controller_calibrate_joystick(adafruitControllerCreationResult.value, 1U, 20).succeeded);

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
    ASSERT_TRUE(iot_controller_refresh_input_state(adafruitControllerCreationResult.value).succeeded);
    const char *joystickDirection = nullptr;
    ASSERT_TRUE(
        iot_controller_joystick_direction(adafruitControllerCreationResult.value, &joystickDirection).succeeded);
    EXPECT_STREQ(joystickDirection, expectedDirections[directionIndex]);
  }

  iot_controller_state_t controllerState{};
  ASSERT_TRUE(iot_controller_read_state(adafruitControllerCreationResult.value, &controllerState).succeeded);
  EXPECT_EQ(controllerState.pressed_buttons_mask, 0x3fU);
  iot_controller_destroy(adafruitControllerCreationResult.value);
}

} // namespace
