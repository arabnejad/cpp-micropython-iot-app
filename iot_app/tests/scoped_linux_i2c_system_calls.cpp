#include "scoped_linux_i2c_system_calls.h"

#include <cstdarg>
#include <fcntl.h>
#include <stdexcept>

namespace {

iot::hardware::ILinuxI2cSystemCalls *activeLinuxI2cSystemCalls = nullptr;

} // namespace

extern "C" int            __real_open(const char *path, int flags, ...);
extern "C" int            __real_close(int fileDescriptor);
extern "C" int            __real_ioctl(int fileDescriptor, unsigned long request, ...);
extern "C" std::ptrdiff_t __real_write(int fileDescriptor, const void *bytes, std::size_t byteCount);
extern "C" std::ptrdiff_t __real_read(int fileDescriptor, void *bytes, std::size_t byteCount);

namespace iot {
namespace tests {

ScopedLinuxI2cSystemCalls::ScopedLinuxI2cSystemCalls(hardware::ILinuxI2cSystemCalls &systemCalls) {
  if (activeLinuxI2cSystemCalls != nullptr) {
    throw std::logic_error("Only one Linux I2C system-call replacement can be active");
  }
  activeLinuxI2cSystemCalls = &systemCalls;
}

ScopedLinuxI2cSystemCalls::~ScopedLinuxI2cSystemCalls() {
  activeLinuxI2cSystemCalls = nullptr;
}

} // namespace tests
} // namespace iot

extern "C" int __wrap_open(const char *path, int flags, ...) {
  va_list arguments;
  va_start(arguments, flags);
  const int mode = (flags & O_CREAT) != 0 ? va_arg(arguments, int) : 0;
  va_end(arguments);
  if (activeLinuxI2cSystemCalls != nullptr) {
    return activeLinuxI2cSystemCalls->openDevice(path, flags);
  }
  return (flags & O_CREAT) != 0 ? __real_open(path, flags, mode) : __real_open(path, flags);
}

extern "C" int __wrap_close(int fileDescriptor) {
  return activeLinuxI2cSystemCalls ? activeLinuxI2cSystemCalls->closeDevice(fileDescriptor)
                                   : __real_close(fileDescriptor);
}

extern "C" int __wrap_ioctl(int fileDescriptor, unsigned long request, ...) {
  va_list arguments;
  va_start(arguments, request);
  const unsigned long argument = va_arg(arguments, unsigned long);
  va_end(arguments);
  return activeLinuxI2cSystemCalls ? activeLinuxI2cSystemCalls->deviceControl(fileDescriptor, request, argument)
                                   : __real_ioctl(fileDescriptor, request, argument);
}

extern "C" std::ptrdiff_t __wrap_write(int fileDescriptor, const void *bytes, std::size_t byteCount) {
  return activeLinuxI2cSystemCalls ? activeLinuxI2cSystemCalls->writeBytes(
                                         fileDescriptor, static_cast<const std::uint8_t *>(bytes), byteCount)
                                   : __real_write(fileDescriptor, bytes, byteCount);
}

extern "C" std::ptrdiff_t __wrap_read(int fileDescriptor, void *bytes, std::size_t byteCount) {
  return activeLinuxI2cSystemCalls
             ? activeLinuxI2cSystemCalls->readBytes(fileDescriptor, static_cast<std::uint8_t *>(bytes), byteCount)
             : __real_read(fileDescriptor, bytes, byteCount);
}
