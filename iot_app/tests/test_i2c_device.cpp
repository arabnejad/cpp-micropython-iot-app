#include "iot/hardware/i2c_device.h"

#include "mock_linux_i2c_system_calls.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace iot {
namespace hardware {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class LinuxI2cDeviceTest : public ::testing::Test {
protected:
  void SetUp() override {
    ON_CALL(linuxCalls_, openDevice(_, _)).WillByDefault(Return(openFileDescriptor_));
    ON_CALL(linuxCalls_, closeDevice(_)).WillByDefault(Return(0));
    EXPECT_CALL(linuxCalls_, deviceControl(_, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Invoke([this](int, unsigned long request, unsigned long argument) {
          if (request == I2C_FUNCS) {
            *reinterpret_cast<unsigned long *>(argument) = adapterFunctions_;
          }
          return 0;
        }));
  }

  NiceMock<tests::MockLinuxI2cSystemCalls> linuxCalls_;
  int                                      openFileDescriptor_{42};
  unsigned long                            adapterFunctions_{I2C_FUNC_I2C};
};

TEST_F(LinuxI2cDeviceTest, ValidatesBusAndAddressBeforeOpeningLinuxDevices) {
  EXPECT_CALL(linuxCalls_, openDevice(_, _)).Times(0);

  EXPECT_THROW(I2cDevice(-1, 0x50U, linuxCalls_), std::invalid_argument);
  EXPECT_THROW(I2cDevice(1, 0x02U, linuxCalls_), std::invalid_argument);
}

TEST_F(LinuxI2cDeviceTest, OpensTheRequestedBusAndSelectsTheAddress) {
  EXPECT_CALL(linuxCalls_, openDevice(::testing::StrEq("/dev/i2c-1"), _)).WillOnce(Return(openFileDescriptor_));
  EXPECT_CALL(linuxCalls_, deviceControl(openFileDescriptor_, I2C_SLAVE, 0x50U)).WillOnce(Return(0));
  EXPECT_CALL(linuxCalls_, closeDevice(openFileDescriptor_)).WillOnce(Return(0));

  I2cDevice i2cDevice(1, 0x50U, linuxCalls_);

  EXPECT_EQ(i2cDevice.devicePath(), "/dev/i2c-1");
  EXPECT_EQ(i2cDevice.busNumber(), 1);
  EXPECT_EQ(i2cDevice.address(), 0x50U);
}

TEST_F(LinuxI2cDeviceTest, ReportsWhenTheLinuxDeviceCannotBeOpened) {
  EXPECT_CALL(linuxCalls_, openDevice(_, _)).WillOnce(Return(-1));
  errno = ENOENT;
  EXPECT_THROW(I2cDevice(1, 0x50U, linuxCalls_), std::runtime_error);
}

TEST_F(LinuxI2cDeviceTest, ClosesTheDeviceWhenItsCapabilitiesCannotBeRead) {
  EXPECT_CALL(linuxCalls_, deviceControl(openFileDescriptor_, I2C_FUNCS, _)).WillOnce(Return(-1));
  EXPECT_CALL(linuxCalls_, closeDevice(openFileDescriptor_));
  errno = EIO;
  EXPECT_THROW(I2cDevice(1, 0x50U, linuxCalls_), std::runtime_error);
}

TEST_F(LinuxI2cDeviceTest, ClosesTheDeviceWhenItsAddressCannotBeSelected) {
  EXPECT_CALL(linuxCalls_, deviceControl(openFileDescriptor_, I2C_SLAVE, 0x50U)).WillOnce(Return(-1));
  EXPECT_CALL(linuxCalls_, closeDevice(openFileDescriptor_));
  errno = EBUSY;
  EXPECT_THROW(I2cDevice(1, 0x50U, linuxCalls_), std::runtime_error);
}

TEST_F(LinuxI2cDeviceTest, ReadsWritesAndPerformsARepeatedStartTransaction) {
  I2cDevice i2cDevice(1, 0x50U, linuxCalls_);

  EXPECT_CALL(linuxCalls_, writeBytes(openFileDescriptor_, _, 2U))
      .WillOnce(Invoke([](int, const std::uint8_t *bytes, std::size_t) {
        EXPECT_EQ(std::vector<std::uint8_t>(bytes, bytes + 2), (std::vector<std::uint8_t>{0xaaU, 0xbbU}));
        return 2;
      }));
  EXPECT_CALL(linuxCalls_, readBytes(openFileDescriptor_, _, 3U))
      .WillOnce(Invoke([](int, std::uint8_t *bytes, std::size_t) {
        std::memcpy(bytes, "\x10\x20\x30", 3U);
        return 3;
      }));
  EXPECT_CALL(linuxCalls_, deviceControl(openFileDescriptor_, I2C_RDWR, _))
      .WillOnce(Invoke([](int, unsigned long, unsigned long argument) {
        auto *transaction = reinterpret_cast<i2c_rdwr_ioctl_data *>(argument);
        EXPECT_EQ(
            std::vector<std::uint8_t>(transaction->msgs[0].buf, transaction->msgs[0].buf + transaction->msgs[0].len),
            (std::vector<std::uint8_t>{0x01U, 0x02U}));
        transaction->msgs[1].buf[0] = 0x12U;
        transaction->msgs[1].buf[1] = 0x34U;
        return 2;
      }));

  i2cDevice.write({0xaaU, 0xbbU});
  EXPECT_EQ(i2cDevice.read(3U), (std::vector<std::uint8_t>{0x10U, 0x20U, 0x30U}));
  EXPECT_EQ(i2cDevice.writeRead({0x01U, 0x02U}, 2U), (std::vector<std::uint8_t>{0x12U, 0x34U}));
}

TEST_F(LinuxI2cDeviceTest, ReportsInvalidAndFailedTransfers) {
  I2cDevice i2cDevice(1, 0x50U, linuxCalls_);

  EXPECT_THROW(i2cDevice.write({}), std::invalid_argument);
  EXPECT_THROW(i2cDevice.read(0U), std::invalid_argument);
  EXPECT_THROW(i2cDevice.writeRead({}, 1U), std::invalid_argument);
  EXPECT_THROW(i2cDevice.writeThenRead({0x01U}, 1U, std::chrono::microseconds(-1)), std::invalid_argument);

  EXPECT_CALL(linuxCalls_, writeBytes(_, _, 2U)).WillOnce(Return(1));
  EXPECT_THROW(i2cDevice.write({0xaaU, 0xbbU}), std::runtime_error);

  EXPECT_CALL(linuxCalls_, readBytes(_, _, 1U)).WillOnce(Return(-1));
  errno = EIO;
  EXPECT_THROW(i2cDevice.read(1U), std::runtime_error);

  adapterFunctions_ = 0;
  I2cDevice deviceWithoutRepeatedStart(1, 0x50U, linuxCalls_);
  EXPECT_THROW(deviceWithoutRepeatedStart.writeRead({0x01U}, 1U), std::runtime_error);
}

} // namespace
} // namespace hardware
} // namespace iot
