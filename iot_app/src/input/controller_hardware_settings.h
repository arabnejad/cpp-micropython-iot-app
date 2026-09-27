#pragma once

#include <array>
#include <cstdint>

namespace iot {
namespace input {
namespace hardwareSettings {

// These are the connections used by the Raspberry Pi 4 image. Change them
// here and rebuild if a board is wired differently.
constexpr int          adafruitI2cBusNumber      = 1;
constexpr std::uint8_t adafruitI2cAddress        = 0x50;
constexpr int          seenGreatOledI2cBusNumber = 1;
constexpr std::uint8_t seenGreatOledI2cAddress   = 0x3c;
constexpr char         gpioDevicePath[]          = "/dev/gpiochip0";
constexpr unsigned     seenGreatDataCommandPin   = 25;
constexpr unsigned     seenGreatResetPin         = 17;
// D/C is held high for I2C. Reset is pulsed low when the OLED opens.
// Read order: up, down, left, right, joystick press, K1, K2, K3.
constexpr std::array<unsigned, 8> seenGreatInputPins{{19, 13, 26, 6, 5, 16, 20, 21}};

} // namespace hardwareSettings
} // namespace input
} // namespace iot
