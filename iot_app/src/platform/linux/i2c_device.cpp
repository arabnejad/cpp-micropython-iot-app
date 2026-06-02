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

/**
 * @file i2c_device.cpp
 *
 * Implements the I2C operations in `I2cDevice` using Linux `/dev/i2c-N` files.
 * No libi2c dependency is needed.
 */

namespace iot {
namespace hardware {
namespace {

constexpr int kLowestNormalAddress  = 0x03;
constexpr int kHighestNormalAddress = 0x77;

/** Formats an I2C address like `0x50`. */
std::string formatAddress(std::uint8_t i2cAddress) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setw(2) << std::setfill('0') << static_cast<int>(i2cAddress);
  return output.str();
}

/** Checks that Linux can represent the requested transfer size. */
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

} // namespace

ILinuxI2cSystemCalls &linuxI2cSystemCalls() {
  static LinuxI2cSystemCalls systemCalls;
  return systemCalls;
}

I2cDevice::I2cDevice(int i2cBusNumber, std::uint8_t i2cAddress)
    : I2cDevice(i2cBusNumber, i2cAddress, linuxI2cSystemCalls()) {}

I2cDevice::I2cDevice(int i2cBusNumber, std::uint8_t i2cAddress, ILinuxI2cSystemCalls &linuxSystemCalls)
    : i2cDevicePath_("/dev/i2c-" + std::to_string(i2cBusNumber)), i2cBusNumber_(i2cBusNumber), i2cAddress_(i2cAddress),
      linuxSystemCalls_(&linuxSystemCalls) {
  if (i2cBusNumber < 0 || i2cBusNumber > 255) {
    throw std::invalid_argument("I2C bus number must be between 0 and 255");
  }
  if (i2cAddress < kLowestNormalAddress || i2cAddress > kHighestNormalAddress) {
    throw std::invalid_argument("I2C address must be between 0x03 and 0x77");
  }

  fileDescriptor_ = linuxSystemCalls_->openDevice(i2cDevicePath_.c_str(), O_RDWR | O_CLOEXEC);
  if (fileDescriptor_ < 0) {
    const int savedError = errno;
    throw std::runtime_error("Could not open " + i2cDevicePath_ + ": " + std::strerror(savedError));
  }

  if (linuxSystemCalls_->deviceControl(fileDescriptor_, I2C_FUNCS,
                                       reinterpret_cast<unsigned long>(&adapterFunctions_)) < 0) {
    const int savedError = errno;
    linuxSystemCalls_->closeDevice(fileDescriptor_);
    fileDescriptor_ = -1;
    throw std::runtime_error("Could not read capabilities from " + i2cDevicePath_ + ": " + std::strerror(savedError));
  }

  // Setting the address only configures this file descriptor. The first real
  // register read is what proves that the device is present.
  if (linuxSystemCalls_->deviceControl(fileDescriptor_, I2C_SLAVE, static_cast<unsigned long>(i2cAddress_)) < 0) {
    const int savedError = errno;
    linuxSystemCalls_->closeDevice(fileDescriptor_);
    fileDescriptor_ = -1;
    throw std::runtime_error("Could not select I2C address " + formatAddress(i2cAddress_) + " on " + i2cDevicePath_ +
                             ": " + std::strerror(savedError));
  }
}

I2cDevice::~I2cDevice() {
  if (fileDescriptor_ >= 0) {
    linuxSystemCalls_->closeDevice(fileDescriptor_);
  }
}

void I2cDevice::write(const std::vector<std::uint8_t> &bytesToWrite) {
  std::lock_guard<std::mutex> lock(transactionMutex_);
  writeUnlocked(bytesToWrite);
}

std::vector<std::uint8_t> I2cDevice::read(std::size_t numberOfBytesToRead) {
  std::lock_guard<std::mutex> lock(transactionMutex_);
  return readUnlocked(numberOfBytesToRead);
}

std::vector<std::uint8_t> I2cDevice::writeRead(const std::vector<std::uint8_t> &bytesToWrite,
                                               std::size_t                      numberOfBytesToRead) {
  validateTransferSize(bytesToWrite.size(), "I2C write");
  validateTransferSize(numberOfBytesToRead, "I2C read");

  std::lock_guard<std::mutex> lock(transactionMutex_);
  if ((adapterFunctions_ & I2C_FUNC_I2C) == 0UL) {
    throw std::runtime_error(i2cDevicePath_ + " does not support combined I2C transactions");
  }

  // Linux's API accepts a writable pointer even though it does not change the
  // data. Use a copy instead of casting away the public const promise.
  std::vector<std::uint8_t> writeBuffer = bytesToWrite;
  std::vector<std::uint8_t> readBuffer(numberOfBytesToRead);

  i2c_msg messages[2]{};
  messages[0].addr  = i2cAddress_;
  messages[0].flags = 0;
  messages[0].len   = static_cast<std::uint16_t>(writeBuffer.size());
  messages[0].buf   = writeBuffer.data();
  messages[1].addr  = i2cAddress_;
  messages[1].flags = I2C_M_RD;
  messages[1].len   = static_cast<std::uint16_t>(readBuffer.size());
  messages[1].buf   = readBuffer.data();

  i2c_rdwr_ioctl_data transaction{};
  transaction.msgs  = messages;
  transaction.nmsgs = 2;

  int result = -1;
  do {
    result = linuxSystemCalls_->deviceControl(fileDescriptor_, I2C_RDWR, reinterpret_cast<unsigned long>(&transaction));
  } while (result < 0 && errno == EINTR);

  if (result != 2) {
    const int savedError = errno;
    throw std::runtime_error("Combined I2C transaction failed for " + formatAddress(i2cAddress_) + " on " +
                             i2cDevicePath_ + ": " + std::strerror(savedError));
  }
  return readBuffer;
}

std::vector<std::uint8_t> I2cDevice::writeThenRead(const std::vector<std::uint8_t> &bytesToWrite,
                                                   std::size_t                      numberOfBytesToRead,
                                                   std::chrono::microseconds        delayBeforeRead) {
  if (delayBeforeRead.count() < 0) {
    throw std::invalid_argument("I2C read delay cannot be negative");
  }

  std::lock_guard<std::mutex> lock(transactionMutex_);
  writeUnlocked(bytesToWrite);
  if (delayBeforeRead.count() > 0) {
    std::this_thread::sleep_for(delayBeforeRead);
  }
  return readUnlocked(numberOfBytesToRead);
}

int I2cDevice::busNumber() const noexcept {
  return i2cBusNumber_;
}

std::uint8_t I2cDevice::address() const noexcept {
  return i2cAddress_;
}

const std::string &I2cDevice::devicePath() const noexcept {
  return i2cDevicePath_;
}

void I2cDevice::writeUnlocked(const std::vector<std::uint8_t> &bytesToWrite) {
  validateTransferSize(bytesToWrite.size(), "I2C write");

  ssize_t bytesWritten = -1;
  do {
    bytesWritten = linuxSystemCalls_->writeBytes(fileDescriptor_, bytesToWrite.data(), bytesToWrite.size());
  } while (bytesWritten < 0 && errno == EINTR);

  if (bytesWritten < 0) {
    const int savedError = errno;
    throw std::runtime_error("I2C write failed for " + formatAddress(i2cAddress_) + " on " + i2cDevicePath_ + ": " +
                             std::strerror(savedError));
  }
  if (static_cast<std::size_t>(bytesWritten) != bytesToWrite.size()) {
    throw std::runtime_error("Short I2C write to " + formatAddress(i2cAddress_) + ": expected " +
                             std::to_string(bytesToWrite.size()) + " bytes, sent " + std::to_string(bytesWritten));
  }
}

std::vector<std::uint8_t> I2cDevice::readUnlocked(std::size_t numberOfBytesToRead) {
  validateTransferSize(numberOfBytesToRead, "I2C read");
  std::vector<std::uint8_t> bytesReadFromDevice(numberOfBytesToRead);

  ssize_t bytesRead = -1;
  do {
    bytesRead = linuxSystemCalls_->readBytes(fileDescriptor_, bytesReadFromDevice.data(), bytesReadFromDevice.size());
  } while (bytesRead < 0 && errno == EINTR);

  if (bytesRead < 0) {
    const int savedError = errno;
    throw std::runtime_error("I2C read failed for " + formatAddress(i2cAddress_) + " on " + i2cDevicePath_ + ": " +
                             std::strerror(savedError));
  }
  if (static_cast<std::size_t>(bytesRead) != numberOfBytesToRead) {
    throw std::runtime_error("Short I2C read from " + formatAddress(i2cAddress_) + ": expected " +
                             std::to_string(numberOfBytesToRead) + " bytes, received " + std::to_string(bytesRead));
  }
  return bytesReadFromDevice;
}

} // namespace hardware
} // namespace iot
