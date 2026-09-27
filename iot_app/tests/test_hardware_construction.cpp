#include "iot/hardware/i2c_device.h"

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

} // namespace
} // namespace hardware
} // namespace iot
