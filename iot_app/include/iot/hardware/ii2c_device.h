#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iot {
namespace hardware {

/*
 * I2C operations available to hardware drivers.
 *
 * Hardware drivers use this interface to read and write I2C data. On Linux,
 * I2cDevice implements the interface and manages the connection to the bus.
 */
class II2cDevice {
public:
  virtual ~II2cDevice() = default;

  virtual void                      write(const std::vector<std::uint8_t> &bytesToWrite)     = 0;
  virtual std::vector<std::uint8_t> read(std::size_t numberOfBytesToRead)                    = 0;
  virtual std::vector<std::uint8_t> writeRead(const std::vector<std::uint8_t> &bytesToWrite,
                                              std::size_t                      numberOfBytesToRead)               = 0;
  virtual std::vector<std::uint8_t> writeThenRead(const std::vector<std::uint8_t> &bytesToWrite,
                                                  std::size_t                      numberOfBytesToRead,
                                                  std::chrono::microseconds        delayBeforeRead) = 0;

  /* Connection details that a hardware driver can expose for diagnostics. */
  virtual int                busNumber() const noexcept  = 0;
  virtual std::uint8_t       address() const noexcept    = 0;
  virtual const std::string &devicePath() const noexcept = 0;
};

} // namespace hardware
} // namespace iot
