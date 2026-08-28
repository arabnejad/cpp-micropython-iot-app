#pragma once

#include "iot/hardware/ilinux_i2c_system_calls.h"

namespace iot {
namespace tests {

/*
 * Routes Linux I2C calls through an ILinuxI2cSystemCalls implementation.
 *
 * Ordinary class tests inject the interface directly. This scoped adapter is
 * only for end-to-end tests that create a gamepad through the public C or
 * MicroPython API, where no C++ constructor argument is available.
 */
class ScopedLinuxI2cSystemCalls {
public:
  explicit ScopedLinuxI2cSystemCalls(hardware::ILinuxI2cSystemCalls &systemCalls);
  ~ScopedLinuxI2cSystemCalls();

  ScopedLinuxI2cSystemCalls(const ScopedLinuxI2cSystemCalls &)            = delete;
  ScopedLinuxI2cSystemCalls &operator=(const ScopedLinuxI2cSystemCalls &) = delete;
};

} // namespace tests
} // namespace iot
