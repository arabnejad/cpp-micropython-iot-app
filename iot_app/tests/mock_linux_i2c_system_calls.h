#pragma once

#include "platform/linux/internal/ilinux_i2c_system_calls.h"

#include <gmock/gmock.h>

namespace iot {
namespace tests {

class MockLinuxI2cSystemCalls : public hardware::internal::ILinuxI2cSystemCalls {
public:
  MOCK_METHOD(int, openDevice, (const char *path, int flags), (override));
  MOCK_METHOD(int, closeDevice, (int fileDescriptor), (override));
  MOCK_METHOD(int, deviceControl, (int fileDescriptor, unsigned long request, unsigned long argument), (override));
  MOCK_METHOD(std::ptrdiff_t, writeBytes, (int fileDescriptor, const std::uint8_t *bytes, std::size_t byteCount),
              (override));
  MOCK_METHOD(std::ptrdiff_t, readBytes, (int fileDescriptor, std::uint8_t *bytes, std::size_t byteCount), (override));
};

} // namespace tests
} // namespace iot
