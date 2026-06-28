#pragma once

#include "platform/linux/internal/ilinux_i2c_system_calls.h"
#include "scoped_linux_i2c_system_calls.h"

#include <gtest/gtest.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include <cstring>
#include <deque>
#include <vector>

namespace iot {
namespace tests {

/* In-memory I2C board used by C++ and MicroPython gamepad-binding tests. */
class SimulatedGamepadI2cBoard final : public hardware::internal::ILinuxI2cSystemCalls {
public:
  int openDevice(const char *, int) override {
    return 7;
  }

  int closeDevice(int) override {
    return 0;
  }

  int deviceControl(int, unsigned long request, unsigned long argument) override {
    if (request == I2C_FUNCS) {
      *reinterpret_cast<unsigned long *>(argument) = I2C_FUNC_I2C;
    }
    return 0;
  }

  std::ptrdiff_t writeBytes(int, const std::uint8_t *, std::size_t byteCount) override {
    return static_cast<std::ptrdiff_t>(byteCount);
  }

  std::ptrdiff_t readBytes(int, std::uint8_t *bytes, std::size_t byteCount) override {
    EXPECT_FALSE(m_queuedReplies.empty());
    const std::vector<std::uint8_t> queuedReply = std::move(m_queuedReplies.front());
    m_queuedReplies.pop_front();
    EXPECT_EQ(queuedReply.size(), byteCount);
    std::memcpy(bytes, queuedReply.data(), byteCount);
    return static_cast<std::ptrdiff_t>(byteCount);
  }

  void addReply(std::vector<std::uint8_t> replyBytes) {
    m_queuedReplies.push_back(std::move(replyBytes));
  }

  void addSuccessfulConnectionReplies(std::uint32_t buttonInputLogicLevels = 0xffffffffU,
                                      std::uint16_t rawXAxisValue = 512U, std::uint16_t rawYAxisValue = 513U) {
    addReply({0x55U});
    addReply({0x16U, 0x6fU, 0x7aU, 0x97U});
    addReply({static_cast<std::uint8_t>(buttonInputLogicLevels >> 24U),
              static_cast<std::uint8_t>(buttonInputLogicLevels >> 16U),
              static_cast<std::uint8_t>(buttonInputLogicLevels >> 8U),
              static_cast<std::uint8_t>(buttonInputLogicLevels)});
    addReply({static_cast<std::uint8_t>(rawXAxisValue >> 8U), static_cast<std::uint8_t>(rawXAxisValue)});
    addReply({static_cast<std::uint8_t>(rawYAxisValue >> 8U), static_cast<std::uint8_t>(rawYAxisValue)});
  }

private:
  std::deque<std::vector<std::uint8_t>> m_queuedReplies;
};

/* Routes the real gamepad driver's Linux I2C calls to this test board. */
class UseSimulatedGamepadI2cBoard {
public:
  explicit UseSimulatedGamepadI2cBoard(SimulatedGamepadI2cBoard &simulatedGamepadBoard)
      : m_scopedCalls(simulatedGamepadBoard) {}

  ~UseSimulatedGamepadI2cBoard() = default;

  UseSimulatedGamepadI2cBoard(const UseSimulatedGamepadI2cBoard &)            = delete;
  UseSimulatedGamepadI2cBoard &operator=(const UseSimulatedGamepadI2cBoard &) = delete;

private:
  ScopedLinuxI2cSystemCalls m_scopedCalls;
};

} // namespace tests
} // namespace iot
