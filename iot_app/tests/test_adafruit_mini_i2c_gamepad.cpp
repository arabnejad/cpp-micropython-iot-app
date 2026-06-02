#include "iot/input/adafruit_mini_i2c_gamepad.h"

#include "mock_i2c_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace iot {
namespace input {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class AdafruitMiniI2cGamepadDriverTest : public ::testing::Test {
protected:
  void SetUp() override {
    EXPECT_CALL(*i2cDevice_, write(_)).Times(::testing::AnyNumber());
  }

  void expectRegisterRead(std::uint8_t moduleAddress, std::uint8_t registerAddress, std::size_t byteCount,
                          std::vector<std::uint8_t> reply) {
    EXPECT_CALL(*i2cDevice_, writeThenRead(std::vector<std::uint8_t>({moduleAddress, registerAddress}), byteCount, _))
        .WillOnce(Return(std::move(reply)));
  }

  void expectInputState(std::uint32_t buttonInputLevels = 0xffffffffU, std::uint16_t rawXAxis = 512U,
                        std::uint16_t rawYAxis = 513U) {
    expectRegisterRead(
        0x01U, 0x04U, 4U,
        {static_cast<std::uint8_t>(buttonInputLevels >> 24U), static_cast<std::uint8_t>(buttonInputLevels >> 16U),
         static_cast<std::uint8_t>(buttonInputLevels >> 8U), static_cast<std::uint8_t>(buttonInputLevels)});
    expectRegisterRead(0x09U, 0x15U, 2U,
                       {static_cast<std::uint8_t>(rawXAxis >> 8U), static_cast<std::uint8_t>(rawXAxis)});
    expectRegisterRead(0x09U, 0x16U, 2U,
                       {static_cast<std::uint8_t>(rawYAxis >> 8U), static_cast<std::uint8_t>(rawYAxis)});
  }

  void expectSuccessfulConnection(std::uint32_t buttonInputLevels = 0xffffffffU, std::uint16_t rawXAxis = 512U,
                                  std::uint16_t rawYAxis = 513U) {
    expectRegisterRead(0x00U, 0x01U, 1U, {0x55U});
    expectRegisterRead(0x00U, 0x02U, 4U, {0x16U, 0x6fU, 0x7aU, 0x97U});
    expectInputState(buttonInputLevels, rawXAxis, rawYAxis);
  }

  NiceMock<tests::MockI2cDevice> *i2cDevice_{new NiceMock<tests::MockI2cDevice>};
  AdafruitMiniI2cGamepad          gamepad_{std::unique_ptr<hardware::II2cDevice>(i2cDevice_)};
};

TEST(AdafruitMiniI2cGamepadConstructionTest, RequiresAnI2cDevice) {
  EXPECT_THROW(AdafruitMiniI2cGamepad(nullptr), std::invalid_argument);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, ConnectsAndReadsItsIdentityAndInitialInputState) {
  EXPECT_CALL(*i2cDevice_, write(std::vector<std::uint8_t>({0x00U, 0x7fU, 0xffU}))).Times(1);
  expectSuccessfulConnection(0xffffffdfU, 512U, 513U);

  EXPECT_FALSE(gamepad_.isConnected());
  gamepad_.connect();

  EXPECT_TRUE(gamepad_.isConnected());
  EXPECT_STREQ(gamepad_.modelName(), "Adafruit Mini I2C STEMMA QT Gamepad");
  EXPECT_EQ(gamepad_.processorHardwareId(), 0x55U);
  EXPECT_EQ(gamepad_.productIdAndFirmwareDateCode(), 0x166f7a97U);
  EXPECT_EQ(gamepad_.firmwareProductId(), 5743U);
  EXPECT_EQ(gamepad_.firmwareDateCode(), 0x7a97U);
  EXPECT_EQ(gamepad_.joystick().position().x, 511);
  EXPECT_EQ(gamepad_.joystick().position().y, 510);
  EXPECT_TRUE(gamepad_.buttons().isPressed(GamepadButton::A));
  EXPECT_FALSE(gamepad_.buttons().isPressed(GamepadButton::B));
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RequiresAConnectionBeforeReadingOrCalibrating) {
  EXPECT_THROW(gamepad_.refreshInputState(), std::logic_error);
  EXPECT_THROW(gamepad_.calibrateJoystick(1U, 100), std::logic_error);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RejectsADeviceWithTheWrongProductId) {
  expectRegisterRead(0x00U, 0x01U, 1U, {0x55U});
  expectRegisterRead(0x00U, 0x02U, 4U, {0x00U, 0x01U, 0x00U, 0x00U});

  EXPECT_THROW(gamepad_.connect(), std::runtime_error);
  EXPECT_FALSE(gamepad_.isConnected());
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RefreshesPressedButtonsAndReversedJoystickAxes) {
  expectSuccessfulConnection();
  gamepad_.connect();

  // X and Start are pressed because their input bits are low.
  expectInputState(0xfffeffbfU, 100U, 900U);
  gamepad_.refreshInputState();

  EXPECT_TRUE(gamepad_.buttons().isPressed(GamepadButton::X));
  EXPECT_TRUE(gamepad_.buttons().isPressed(GamepadButton::Start));
  EXPECT_EQ(gamepad_.joystick().position().x, 923);
  EXPECT_EQ(gamepad_.joystick().position().y, 123);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RejectsAJoystickCalibrationWithNoSamples) {
  expectSuccessfulConnection();
  gamepad_.connect();

  EXPECT_THROW(gamepad_.calibrateJoystick(0U, 100), std::invalid_argument);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, CalibratesTheJoystickFromTheRequestedSamples) {
  expectSuccessfulConnection();
  gamepad_.connect();

  expectRegisterRead(0x09U, 0x15U, 2U, {0x01U, 0x00U});
  expectRegisterRead(0x09U, 0x16U, 2U, {0x02U, 0x00U});
  gamepad_.calibrateJoystick(1U, 25);

  EXPECT_EQ(gamepad_.joystick().centre().x, 767);
  EXPECT_EQ(gamepad_.joystick().centre().y, 511);
  EXPECT_EQ(gamepad_.joystick().deadZone(), 25);
  EXPECT_EQ(gamepad_.joystick().position().x, 767);
  EXPECT_EQ(gamepad_.joystick().position().y, 511);
}

} // namespace
} // namespace input
} // namespace iot
