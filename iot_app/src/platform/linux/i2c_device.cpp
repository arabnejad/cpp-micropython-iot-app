#include "iot/hardware/i2c_device.h"

#include <fcntl.h>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

/*
 * Implements I2cDevice through the Linux /dev/i2c-N interface.
 * No libi2c dependency is needed.
 */

namespace iot {
namespace hardware {
namespace {

constexpr int kLowestNormalAddress  = 0x03;
constexpr int kHighestNormalAddress = 0x77;

/* Formats an I2C address such as 0x50. */
std::string formatAddress(std::uint8_t i2cAddress) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setw(2) << std::setfill('0') << static_cast<int>(i2cAddress);
  return output.str();
}

/* Checks that Linux can represent the requested transfer size. */
void validateTransferSize(std::size_t size, const std::string &operation) {
  if (size == 0U) {
    throw std::invalid_argument(operation + " requires at least one byte");
  }
  if (size > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument(operation + " is too large for one I2C transfer");
  }
}

class LinuxI2cSystemCalls final : public ILinuxI2cSystemCalls {
public:
  int openDevice(const char *path, int flags) override {
    return ::open(path, flags);
  }

  int closeDevice(int fileDescriptor) override {
    return ::close(fileDescriptor);
  }

  int deviceControl(int fileDescriptor, unsigned long request, unsigned long argument) override {
    return ::ioctl(fileDescriptor, request, argument);
  }

  std::ptrdiff_t writeBytes(int fileDescriptor, const std::uint8_t *bytes, std::size_t byteCount) override {
    return ::write(fileDescriptor, bytes, byteCount);
  }

  std::ptrdiff_t readBytes(int fileDescriptor, std::uint8_t *bytes, std::size_t byteCount) override {
    return ::read(fileDescriptor, bytes, byteCount);
  }
};

ILinuxI2cSystemCalls &defaultLinuxI2cSystemCalls() {
  static LinuxI2cSystemCalls systemCalls;
  return systemCalls;
}

} // namespace

I2cDevice::I2cDevice(int i2cBusNumber, std::uint8_t i2cAddress)
    : I2cDevice(i2cBusNumber, i2cAddress, defaultLinuxI2cSystemCalls()) {}

I2cDevice::I2cDevice(int i2cBusNumber, std::uint8_t i2cAddress, ILinuxI2cSystemCalls &linuxSystemCalls)
    : m_i2cDevicePath("/dev/i2c-" + std::to_string(i2cBusNumber)), m_i2cBusNumber(i2cBusNumber),
      m_i2cAddress(i2cAddress), m_linuxSystemCalls(&linuxSystemCalls) {
  if (i2cBusNumber < 0 || i2cBusNumber > 255) {
    throw std::invalid_argument("I2C bus number must be between 0 and 255");
  }
  if (i2cAddress < kLowestNormalAddress || i2cAddress > kHighestNormalAddress) {
    throw std::invalid_argument("I2C address must be between 0x03 and 0x77");
  }

  m_fileDescriptor = m_linuxSystemCalls->openDevice(m_i2cDevicePath.c_str(), O_RDWR | O_CLOEXEC);
  if (m_fileDescriptor < 0) {
    const int savedError = errno;
    throw std::runtime_error("Could not open " + m_i2cDevicePath + ": " + std::strerror(savedError));
  }

  if (m_linuxSystemCalls->deviceControl(m_fileDescriptor, I2C_FUNCS,
                                        reinterpret_cast<unsigned long>(&m_adapterFunctions)) < 0) {
    const int savedError = errno;
    m_linuxSystemCalls->closeDevice(m_fileDescriptor);
    m_fileDescriptor = -1;
    throw std::runtime_error("Could not read capabilities from " + m_i2cDevicePath + ": " + std::strerror(savedError));
  }

  // Setting the address only configures this file descriptor. The first real
  // register read is what proves that the device is present.
  if (m_linuxSystemCalls->deviceControl(m_fileDescriptor, I2C_SLAVE, static_cast<unsigned long>(m_i2cAddress)) < 0) {
    const int savedError = errno;
    m_linuxSystemCalls->closeDevice(m_fileDescriptor);
    m_fileDescriptor = -1;
    throw std::runtime_error("Could not select I2C address " + formatAddress(m_i2cAddress) + " on " + m_i2cDevicePath +
                             ": " + std::strerror(savedError));
  }
}

I2cDevice::~I2cDevice() {
  if (m_fileDescriptor >= 0) {
    m_linuxSystemCalls->closeDevice(m_fileDescriptor);
  }
}

void I2cDevice::write(const std::vector<std::uint8_t> &bytesToWrite) {
  std::lock_guard<std::mutex> lock(m_transactionMutex);
  writeUnlocked(bytesToWrite);
}

std::vector<std::uint8_t> I2cDevice::read(std::size_t numberOfBytesToRead) {
  std::lock_guard<std::mutex> lock(m_transactionMutex);
  return readUnlocked(numberOfBytesToRead);
}

std::vector<std::uint8_t> I2cDevice::writeRead(const std::vector<std::uint8_t> &bytesToWrite,
                                               std::size_t                      numberOfBytesToRead) {
  validateTransferSize(bytesToWrite.size(), "I2C write");
  validateTransferSize(numberOfBytesToRead, "I2C read");

  std::lock_guard<std::mutex> lock(m_transactionMutex);
  if ((m_adapterFunctions & I2C_FUNC_I2C) == 0UL) {
    throw std::runtime_error(m_i2cDevicePath + " does not support combined I2C transactions");
  }

  // Linux's API accepts a writable pointer even though it does not change the
  // data. Use a copy instead of casting away the public const promise.
  std::vector<std::uint8_t> writeBuffer = bytesToWrite;
  std::vector<std::uint8_t> readBuffer(numberOfBytesToRead);

  i2c_msg messages[2]{};
  messages[0].addr  = m_i2cAddress;
  messages[0].flags = 0;
  messages[0].len   = static_cast<std::uint16_t>(writeBuffer.size());
  messages[0].buf   = writeBuffer.data();
  messages[1].addr  = m_i2cAddress;
  messages[1].flags = I2C_M_RD;
  messages[1].len   = static_cast<std::uint16_t>(readBuffer.size());
  messages[1].buf   = readBuffer.data();

  i2c_rdwr_ioctl_data transaction{};
  transaction.msgs  = messages;
  transaction.nmsgs = 2;

  int result = -1;
  do {
    result =
        m_linuxSystemCalls->deviceControl(m_fileDescriptor, I2C_RDWR, reinterpret_cast<unsigned long>(&transaction));
  } while (result < 0 && errno == EINTR);

  if (result != 2) {
    const int savedError = errno;
    throw std::runtime_error("Combined I2C transaction failed for " + formatAddress(m_i2cAddress) + " on " +
                             m_i2cDevicePath + ": " + std::strerror(savedError));
  }
  return readBuffer;
}

std::vector<std::uint8_t> I2cDevice::writeThenRead(const std::vector<std::uint8_t> &bytesToWrite,
                                                   std::size_t                      numberOfBytesToRead,
                                                   std::chrono::microseconds        delayBeforeRead) {
  if (delayBeforeRead.count() < 0) {
    throw std::invalid_argument("I2C read delay cannot be negative");
  }

  std::lock_guard<std::mutex> lock(m_transactionMutex);
  writeUnlocked(bytesToWrite);
  if (delayBeforeRead.count() > 0) {
    std::this_thread::sleep_for(delayBeforeRead);
  }
  return readUnlocked(numberOfBytesToRead);
}

int I2cDevice::busNumber() const noexcept {
  return m_i2cBusNumber;
}

std::uint8_t I2cDevice::address() const noexcept {
  return m_i2cAddress;
}

const std::string &I2cDevice::devicePath() const noexcept {
  return m_i2cDevicePath;
}

void I2cDevice::writeUnlocked(const std::vector<std::uint8_t> &bytesToWrite) {
  validateTransferSize(bytesToWrite.size(), "I2C write");

  ssize_t bytesWritten = -1;
  do {
    bytesWritten = m_linuxSystemCalls->writeBytes(m_fileDescriptor, bytesToWrite.data(), bytesToWrite.size());
  } while (bytesWritten < 0 && errno == EINTR);

  if (bytesWritten < 0) {
    const int savedError = errno;
    throw std::runtime_error("I2C write failed for " + formatAddress(m_i2cAddress) + " on " + m_i2cDevicePath + ": " +
                             std::strerror(savedError));
  }
  if (static_cast<std::size_t>(bytesWritten) != bytesToWrite.size()) {
    throw std::runtime_error("Short I2C write to " + formatAddress(m_i2cAddress) + ": expected " +
                             std::to_string(bytesToWrite.size()) + " bytes, sent " + std::to_string(bytesWritten));
  }
}

std::vector<std::uint8_t> I2cDevice::readUnlocked(std::size_t numberOfBytesToRead) {
  validateTransferSize(numberOfBytesToRead, "I2C read");
  std::vector<std::uint8_t> bytesReadFromDevice(numberOfBytesToRead);

  ssize_t bytesRead = -1;
  do {
    bytesRead = m_linuxSystemCalls->readBytes(m_fileDescriptor, bytesReadFromDevice.data(), bytesReadFromDevice.size());
  } while (bytesRead < 0 && errno == EINTR);

  if (bytesRead < 0) {
    const int savedError = errno;
    throw std::runtime_error("I2C read failed for " + formatAddress(m_i2cAddress) + " on " + m_i2cDevicePath + ": " +
                             std::strerror(savedError));
  }
  if (static_cast<std::size_t>(bytesRead) != numberOfBytesToRead) {
    throw std::runtime_error("Short I2C read from " + formatAddress(m_i2cAddress) + ": expected " +
                             std::to_string(numberOfBytesToRead) + " bytes, received " + std::to_string(bytesRead));
  }
  return bytesReadFromDevice;
}

} // namespace hardware
} // namespace iot
