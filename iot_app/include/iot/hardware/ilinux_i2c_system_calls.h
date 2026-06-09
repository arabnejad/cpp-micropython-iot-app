#pragma once

#include <cstddef>
#include <cstdint>

namespace iot {
namespace hardware {

/*
 * Internal Linux operations used by I2cDevice.
 *
 * Hardware drivers use II2cDevice. This lower-level wrapper only separates the
 * operating-system calls from the I2C transaction code.
 */
class ILinuxI2cSystemCalls {
public:
  virtual ~ILinuxI2cSystemCalls() = default;

  virtual int            openDevice(const char *path, int flags)                                          = 0;
  virtual int            closeDevice(int fileDescriptor)                                                  = 0;
  virtual int            deviceControl(int fileDescriptor, unsigned long request, unsigned long argument) = 0;
  virtual std::ptrdiff_t writeBytes(int fileDescriptor, const std::uint8_t *bytes, std::size_t byteCount) = 0;
  virtual std::ptrdiff_t readBytes(int fileDescriptor, std::uint8_t *bytes, std::size_t byteCount)        = 0;
};

} // namespace hardware
} // namespace iot
