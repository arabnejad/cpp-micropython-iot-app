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
    EXPECT_CALL(*m_i2cDevice, write(_)).Times(::testing::AnyNumber());
  }

  void expectRegisterRead(std::uint8_t moduleAddress, std::uint8_t registerAddress, std::size_t byteCount,
                          std::vector<std::uint8_t> reply) {
    EXPECT_CALL(*m_i2cDevice, writeThenRead(std::vector<std::uint8_t>({moduleAddress, registerAddress}), byteCount, _))
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

  NiceMock<tests::MockI2cDevice> *m_i2cDevice{new NiceMock<tests::MockI2cDevice>};
  AdafruitMiniI2cGamepad          m_gamepad{std::unique_ptr<hardware::II2cDevice>(m_i2cDevice)};
};

TEST(AdafruitMiniI2cGamepadConstructionTest, RequiresAnI2cDevice) {
  EXPECT_THROW(AdafruitMiniI2cGamepad(nullptr), std::invalid_argument);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, ConnectsAndReadsItsIdentityAndInitialInputState) {
  EXPECT_CALL(*m_i2cDevice, write(std::vector<std::uint8_t>({0x00U, 0x7fU, 0xffU}))).Times(1);
  expectSuccessfulConnection(0xffffffdfU, 512U, 513U);

  EXPECT_FALSE(m_gamepad.isConnected());
  m_gamepad.connect();

  EXPECT_TRUE(m_gamepad.isConnected());
  EXPECT_STREQ(m_gamepad.modelName(), "Adafruit Mini I2C STEMMA QT Gamepad");
  EXPECT_EQ(m_gamepad.processorHardwareId(), 0x55U);
  EXPECT_EQ(m_gamepad.productIdAndFirmwareDateCode(), 0x166f7a97U);
  EXPECT_EQ(m_gamepad.firmwareProductId(), 5743U);
  EXPECT_EQ(m_gamepad.firmwareDateCode(), 0x7a97U);
  EXPECT_EQ(m_gamepad.joystick().position().x, 511);
  EXPECT_EQ(m_gamepad.joystick().position().y, 510);
  EXPECT_TRUE(m_gamepad.buttons().isPressed(GamepadButton::A));
  EXPECT_FALSE(m_gamepad.buttons().isPressed(GamepadButton::B));
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RequiresAConnectionBeforeReadingOrCalibrating) {
  EXPECT_THROW(m_gamepad.refreshInputState(), std::logic_error);
  EXPECT_THROW(m_gamepad.calibrateJoystick(1U, 100), std::logic_error);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RejectsADeviceWithTheWrongProductId) {
  expectRegisterRead(0x00U, 0x01U, 1U, {0x55U});
  expectRegisterRead(0x00U, 0x02U, 4U, {0x00U, 0x01U, 0x00U, 0x00U});

  EXPECT_THROW(m_gamepad.connect(), std::runtime_error);
  EXPECT_FALSE(m_gamepad.isConnected());
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RefreshesPressedButtonsAndReversedJoystickAxes) {
  expectSuccessfulConnection();
  m_gamepad.connect();

  // X and Start are pressed because their input bits are low.
  expectInputState(0xfffeffbfU, 100U, 900U);
  m_gamepad.refreshInputState();

  EXPECT_TRUE(m_gamepad.buttons().isPressed(GamepadButton::X));
  EXPECT_TRUE(m_gamepad.buttons().isPressed(GamepadButton::Start));
  EXPECT_EQ(m_gamepad.joystick().position().x, 923);
  EXPECT_EQ(m_gamepad.joystick().position().y, 123);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, RejectsAJoystickCalibrationWithNoSamples) {
  expectSuccessfulConnection();
  m_gamepad.connect();

  EXPECT_THROW(m_gamepad.calibrateJoystick(0U, 100), std::invalid_argument);
}

TEST_F(AdafruitMiniI2cGamepadDriverTest, CalibratesTheJoystickFromTheRequestedSamples) {
  expectSuccessfulConnection();
  m_gamepad.connect();

  expectRegisterRead(0x09U, 0x15U, 2U, {0x01U, 0x00U});
  expectRegisterRead(0x09U, 0x16U, 2U, {0x02U, 0x00U});
  m_gamepad.calibrateJoystick(1U, 25);

  EXPECT_EQ(m_gamepad.joystick().centre().x, 767);
  EXPECT_EQ(m_gamepad.joystick().centre().y, 511);
  EXPECT_EQ(m_gamepad.joystick().deadZone(), 25);
  EXPECT_EQ(m_gamepad.joystick().position().x, 767);
  EXPECT_EQ(m_gamepad.joystick().position().y, 511);
}

} // namespace
} // namespace input
} // namespace iot
