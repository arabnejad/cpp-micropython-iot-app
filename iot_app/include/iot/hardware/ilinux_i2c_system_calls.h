#pragma once

#include <cstddef>
#include <cstdint>

namespace iot {
namespace hardware {

/**
 * Linux operations used by `I2cDevice`.
 *
 * This small boundary keeps operating-system calls separate from the I2C
 * transaction logic.
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

/** Returns the Linux implementation used by the application. */
ILinuxI2cSystemCalls &linuxI2cSystemCalls();

} // namespace hardware
} // namespace iot
