#pragma once

#include <cstddef>
#include <cstdint>

namespace iot {
namespace hardware {
namespace internal {

/*
 * Internal Linux operations used by I2cDevice.
 *
 * The normal implementation calls the Linux functions directly. Unit tests
 * can replace those calls through this interface to check read, write, ioctl,
 * and error paths without opening a real I2C bus. Hardware drivers should use
 * II2cDevice instead.
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

} // namespace internal
} // namespace hardware
} // namespace iot
