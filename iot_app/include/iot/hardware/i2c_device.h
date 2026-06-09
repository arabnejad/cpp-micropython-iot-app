#pragma once

#include "iot/hardware/ii2c_device.h"
#include "iot/hardware/ilinux_i2c_system_calls.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace iot {
namespace hardware {

/*
 * Opens one device on a Linux I2C bus and provides basic read/write operations.
 *
 * The bus number and seven-bit address identify the device. For example,
 * bus 1 and address 0x50 open /dev/i2c-1 and select address 0x50. The file
 * descriptor is closed when this object is destroyed.
 */
class I2cDevice : public II2cDevice {
public:
  /* Opens the Linux I2C device using the normal system calls. */
  I2cDevice(int i2cBusNumber, std::uint8_t i2cAddress);
  /* Opens the device using the supplied Linux-call implementation. */
  I2cDevice(int i2cBusNumber, std::uint8_t i2cAddress, ILinuxI2cSystemCalls &linuxSystemCalls);
  ~I2cDevice() override;

  // Owns one open file descriptor; copying and moving are disabled.
  I2cDevice(const I2cDevice &)            = delete;
  I2cDevice &operator=(const I2cDevice &) = delete;
  I2cDevice(I2cDevice &&)                 = delete;
  I2cDevice &operator=(I2cDevice &&)      = delete;

  /* Sends the complete byte array in bytesToWrite in one transfer. */
  void write(const std::vector<std::uint8_t> &bytesToWrite) override;

  /* Reads exactly numberOfBytesToRead bytes from the device. */
  std::vector<std::uint8_t> read(std::size_t numberOfBytesToRead) override;

  /*
   * Writes and reads as one transaction, using a repeated start.
   *
   * Use this when a device does not allow a stop condition between the write
   * and the following read.
   */
  std::vector<std::uint8_t> writeRead(const std::vector<std::uint8_t> &bytesToWrite,
                                      std::size_t                      numberOfBytesToRead) override;

  /*
   * Writes a command, waits, and then reads the reply without allowing another
   * caller to use this device in between.
   *
   * Unlike writeRead(), this uses two separate transfers. The gamepad needs
   * this delay to prepare data after a register is selected.
   */
  std::vector<std::uint8_t> writeThenRead(const std::vector<std::uint8_t> &bytesToWrite,
                                          std::size_t                      numberOfBytesToRead,
                                          std::chrono::microseconds        delayBeforeRead) override;

  /* Gets the Linux I2C bus number. */
  int busNumber() const noexcept override;

  /* Gets this device's seven-bit I2C address. */
  std::uint8_t address() const noexcept override;

  /* Gets the Linux device path, such as /dev/i2c-1 */
  const std::string &devicePath() const noexcept override;

private:
  /* Writes while the caller is already holding m_transactionMutex */
  void writeUnlocked(const std::vector<std::uint8_t> &bytesToWrite);

  /* Reads while the caller is already holding m_transactionMutex */
  std::vector<std::uint8_t> readUnlocked(std::size_t numberOfBytesToRead);

  std::string  m_i2cDevicePath;
  int          m_fileDescriptor{-1};
  int          m_i2cBusNumber{0};
  std::uint8_t m_i2cAddress{0};
  /* Feature flags reported by the Linux adapter through I2C_FUNCS. */
  unsigned long         m_adapterFunctions{0};
  ILinuxI2cSystemCalls *m_linuxSystemCalls{nullptr};
  std::mutex            m_transactionMutex;
};

} // namespace hardware
} // namespace iot
