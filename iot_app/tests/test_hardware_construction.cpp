#include "iot/hardware/i2c_device.h"
#include "iot/input/adafruit_mini_i2c_gamepad.h"

#include <gtest/gtest.h>

namespace iot {
namespace hardware {
namespace {

TEST(I2cDeviceTest, RejectsBusNumbersOutsideTheLinuxDeviceRangeBeforeOpeningADevice) {
  EXPECT_THROW(I2cDevice(-1, 0x50U), std::invalid_argument);
  EXPECT_THROW(I2cDevice(256, 0x50U), std::invalid_argument);
}

TEST(I2cDeviceTest, RejectsAddressesOutsideTheNormalI2cAddressRangeBeforeOpeningADevice) {
  EXPECT_THROW(I2cDevice(1, 0x02U), std::invalid_argument);
  EXPECT_THROW(I2cDevice(1, 0x78U), std::invalid_argument);
}

TEST(AdafruitMiniI2cGamepadTest, ValidatesItsI2cBusBeforeTryingToOpenTheGamepad) {
  EXPECT_THROW(input::AdafruitMiniI2cGamepad(256, 0x50U), std::invalid_argument);
}

} // namespace
} // namespace hardware
} // namespace iot
