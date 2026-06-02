#pragma once

#include "iot/hardware/ii2c_device.h"

#include <gmock/gmock.h>

namespace iot {
namespace tests {

class MockI2cDevice : public hardware::II2cDevice {
public:
  MOCK_METHOD(void, write, (const std::vector<std::uint8_t> &data), (override));
  MOCK_METHOD(std::vector<std::uint8_t>, read, (std::size_t byteCount), (override));
  MOCK_METHOD(std::vector<std::uint8_t>, writeRead,
              (const std::vector<std::uint8_t> &writeData, std::size_t readByteCount), (override));
  MOCK_METHOD(std::vector<std::uint8_t>, writeThenRead,
              (const std::vector<std::uint8_t> &writeData, std::size_t readByteCount,
               std::chrono::microseconds delayBeforeRead),
              (override));
  MOCK_METHOD(int, busNumber, (), (const, noexcept, override));
  MOCK_METHOD(std::uint8_t, address, (), (const, noexcept, override));
  MOCK_METHOD(const std::string &, devicePath, (), (const, noexcept, override));
};

} // namespace tests
} // namespace iot
