#include "iot/input/adafruit_mini_i2c_gamepad.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

/**
 * @file adafruit_mini_i2c_gamepad.cpp
 *
 * Implements the Seesaw commands needed by Adafruit gamepad 5743. The register
 * order and delays match the test program that worked on the Raspberry Pi.
 */

namespace iot {
namespace input {
namespace {

// These values come from Adafruit's Seesaw driver:
// https://github.com/adafruit/Adafruit_Seesaw/blob/master/Adafruit_seesaw.h
constexpr std::uint8_t statusModuleAddress = 0x00; // SEESAW_STATUS_BASE
constexpr std::uint8_t hardwareId          = 0x01; // SEESAW_STATUS_HW_ID
constexpr std::uint8_t productAndDateCode  = 0x02; // SEESAW_STATUS_VERSION
constexpr std::uint8_t softwareReset       = 0x7f; // SEESAW_STATUS_SWRST

constexpr std::uint8_t gpioModuleAddress  = 0x01; // SEESAW_GPIO_BASE
constexpr std::uint8_t gpioDirectionClear = 0x03; // SEESAW_GPIO_DIRCLR_BULK
constexpr std::uint8_t gpioBulkRead       = 0x04; // SEESAW_GPIO_BULK
constexpr std::uint8_t gpioBulkSet        = 0x05; // SEESAW_GPIO_BULK_SET
constexpr std::uint8_t gpioPullEnable     = 0x0b; // SEESAW_GPIO_PULLENSET

constexpr std::uint8_t adcModuleAddress = 0x09; // SEESAW_ADC_BASE
constexpr std::uint8_t adcChannelOffset = 0x07; // SEESAW_ADC_CHANNEL_OFFSET

// Adafruit identifies this board with product ID 5743 (_5743_PID):
// https://github.com/adafruit/Adafruit_CircuitPython_seesaw/blob/main/adafruit_seesaw/seesaw.py
constexpr std::uint16_t adafruitMiniI2cGamepadProductId = 5743; // _5743_PID

// The board returns the product and firmware date in one 32-bit number. The
// upper 16 bits are the product ID. The lower 16 bits are a date packed into
// bits instead of stored as text:
// bits 0-5 store the two-digit year, bits 7-10 store the month, and bits 11-15
// store the day.
//
//   31                       16 15                         0
//   +--------------------------+----------------------------+
//   | Product ID               | Encoded firmware date      |
//   +--------------------------+----------------------------+
//
// The gamepad used during development returned 0x166F7A97:
//
//   0x166F = 5743          product ID
//   0x7A97 = 2023-05-15    encoded firmware date
//
// Adafruit's date-code implementation is here:
// https://github.com/adafruit/Adafruit_Seesaw/blob/master/Adafruit_seesaw.cpp#L172-L179

// Adafruit calls these input numbers "pins" in its examples:
// https://learn.adafruit.com/gamepad-qt/circuitpython-and-python
constexpr int buttonXInputNumber      = 6;  // BUTTON_X
constexpr int buttonYInputNumber      = 2;  // BUTTON_Y
constexpr int buttonAInputNumber      = 5;  // BUTTON_A
constexpr int buttonBInputNumber      = 1;  // BUTTON_B
constexpr int buttonSelectInputNumber = 0;  // BUTTON_SELECT
constexpr int buttonStartInputNumber  = 16; // BUTTON_START

// Each joystick axis returns 0 to 1023. The board is mounted so the raw values
// run backwards, so `1023 - rawValue` makes right and up increase the result.
constexpr std::uint8_t joystickXAxisAnalogInputNumber = 14;   // Joystick X analog input
constexpr std::uint8_t joystickYAxisAnalogInputNumber = 15;   // Joystick Y analog input
constexpr int          joystickAxisMaximumValue       = 1023; // Maximum 10-bit ADC value

// These numbers come from the board wiring. The buttons are active-low, which
// means zero is pressed. The driver converts those input bits into the button
// bits defined by `GamepadButton`. See
// `iot_app/docs/hardware/README.md` for the complete example.

/** Makes the bit used to select one numbered button input. */
constexpr std::uint32_t createButtonInputMask(int inputNumber) {
  return static_cast<std::uint32_t>(1UL << inputNumber);
}

/** Bit mask that selects all six button inputs at once. */
constexpr std::uint32_t allGamepadButtonInputsMask =
    createButtonInputMask(buttonXInputNumber) | createButtonInputMask(buttonYInputNumber) |
    createButtonInputMask(buttonAInputNumber) | createButtonInputMask(buttonBInputNumber) |
    createButtonInputMask(buttonSelectInputNumber) | createButtonInputMask(buttonStartInputNumber);

/**
 * Splits a 32-bit number into the four bytes expected by the gamepad.
 *
 * For example, `0x12345678` becomes `{0x12, 0x34, 0x56, 0x78}`. The gamepad
 * expects the highest byte first, which is called big-endian order.
 */
std::vector<std::uint8_t> encodeUint32AsBigEndianBytes(std::uint32_t value) {
  return {
      static_cast<std::uint8_t>((value >> 24U) & 0xffU),
      static_cast<std::uint8_t>((value >> 16U) & 0xffU),
      static_cast<std::uint8_t>((value >> 8U) & 0xffU),
      static_cast<std::uint8_t>(value & 0xffU),
  };
}

/**
 * Joins four bytes received from the gamepad into one 32-bit number.
 *
 * For example, `{0x12, 0x34, 0x56, 0x78}` becomes `0x12345678`. The first byte
 * is the highest part of the number because the gamepad uses big-endian order.
 * The function throws if it does not receive exactly four bytes.
 */
std::uint32_t decodeBigEndianBytesAsUint32(const std::vector<std::uint8_t> &bigEndianBytes) {
  if (bigEndianBytes.size() != 4U) {
    throw std::logic_error("Expected four bytes for a 32-bit gamepad value");
  }
  return (static_cast<std::uint32_t>(bigEndianBytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bigEndianBytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bigEndianBytes[2]) << 8U) | static_cast<std::uint32_t>(bigEndianBytes[3]);
}

void markButtonPressedIfInputIsLow(std::uint32_t buttonInputLevels, int buttonInputNumber, GamepadButton gamepadButton,
                                   std::uint32_t &pressedButtonsMask) {
  // The buttons are active-low, so a zero bit means pressed.
  if ((buttonInputLevels & createButtonInputMask(buttonInputNumber)) == 0U) {
    pressedButtonsMask |= static_cast<std::uint32_t>(gamepadButton);
  }
}

} // namespace

AdafruitMiniI2cGamepad::AdafruitMiniI2cGamepad(int i2cBusNumber, std::uint8_t i2cAddress)
    : AdafruitMiniI2cGamepad(std::make_unique<hardware::I2cDevice>(i2cBusNumber, i2cAddress)) {}

AdafruitMiniI2cGamepad::AdafruitMiniI2cGamepad(std::unique_ptr<hardware::II2cDevice> gamepadI2cDevice)
    : gamepadI2cDevice_(std::move(gamepadI2cDevice)) {
  if (!gamepadI2cDevice_) {
    throw std::invalid_argument("Adafruit gamepad requires an I2C device");
  }
}

const char *AdafruitMiniI2cGamepad::modelName() const noexcept {
  return "Adafruit Mini I2C STEMMA QT Gamepad";
}

void AdafruitMiniI2cGamepad::connect() {
  isConnected_ = false;

  // Read real identity registers instead of sending an SMBus probe. This proves
  // the expected device is present and works on adapters without Quick Write.
  resetGamepadProcessor();
  const std::uint8_t  reportedHardwareId                   = readProcessorHardwareIdFromDevice();
  const std::uint32_t reportedProductIdAndFirmwareDateCode = readProductIdAndFirmwareDateCodeFromDevice();
  const std::uint16_t reportedProductId =
      static_cast<std::uint16_t>((reportedProductIdAndFirmwareDateCode >> 16U) & 0xffffU);
  if (reportedProductId != adafruitMiniI2cGamepadProductId) {
    throw std::runtime_error("The I2C device reports product ID " + std::to_string(reportedProductId) +
                             ", not Adafruit Mini I2C Gamepad product 5743");
  }

  processorHardwareId_          = reportedHardwareId;
  productIdAndFirmwareDateCode_ = reportedProductIdAndFirmwareDateCode;

  // Prepare the six button connections so their pressed state can be read.
  configureButtonInputs();

  // Read the buttons once so they have a valid state as soon as connect() returns.
  updateButtons(readPressedButtonMask());

  // Read the joystick once so it has a valid position as soon as connect() returns.
  updateJoystick(readJoystickPosition());
  isConnected_ = true;
}

void AdafruitMiniI2cGamepad::calibrateJoystick(std::size_t numberOfCalibrationSamples, int joystickDeadZone) {
  throwIfGamepadIsNotConnected();
  if (numberOfCalibrationSamples == 0U) {
    throw std::invalid_argument("Joystick calibration requires at least one sample");
  }

  std::int64_t sumOfXAxisSamples = 0;
  std::int64_t sumOfYAxisSamples = 0;
  for (std::size_t sampleIndex = 0; sampleIndex < numberOfCalibrationSamples; ++sampleIndex) {
    const JoystickPosition sampledJoystickPosition = readJoystickPosition();
    sumOfXAxisSamples += sampledJoystickPosition.x;
    sumOfYAxisSamples += sampledJoystickPosition.y;
    if (sampleIndex + 1U < numberOfCalibrationSamples) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  const JoystickPosition measuredJoystickCentre{
      static_cast<int>(sumOfXAxisSamples / static_cast<std::int64_t>(numberOfCalibrationSamples)),
      static_cast<int>(sumOfYAxisSamples / static_cast<std::int64_t>(numberOfCalibrationSamples)),
  };
  setJoystickCalibration(measuredJoystickCentre, joystickDeadZone);
  updateJoystick(measuredJoystickCentre);
}

void AdafruitMiniI2cGamepad::refreshInputState() {
  throwIfGamepadIsNotConnected();
  updateButtons(readPressedButtonMask());
  updateJoystick(readJoystickPosition());
}

bool AdafruitMiniI2cGamepad::isConnected() const noexcept {
  return isConnected_;
}

std::uint8_t AdafruitMiniI2cGamepad::processorHardwareId() const noexcept {
  return processorHardwareId_;
}

std::uint32_t AdafruitMiniI2cGamepad::productIdAndFirmwareDateCode() const noexcept {
  return productIdAndFirmwareDateCode_;
}

std::uint16_t AdafruitMiniI2cGamepad::firmwareProductId() const noexcept {
  return static_cast<std::uint16_t>((productIdAndFirmwareDateCode_ >> 16U) & 0xffffU);
}

std::uint16_t AdafruitMiniI2cGamepad::firmwareDateCode() const noexcept {
  return static_cast<std::uint16_t>(productIdAndFirmwareDateCode_ & 0xffffU);
}

void AdafruitMiniI2cGamepad::throwIfGamepadIsNotConnected() const {
  if (!isConnected_) {
    throw std::logic_error("AdafruitMiniI2cGamepad must be connected before it can be read");
  }
}

void AdafruitMiniI2cGamepad::resetGamepadProcessor() {
  writeRegister(statusModuleAddress, softwareReset, {0xff});
  // The tested Raspberry Pi program waits 500 ms after this reset.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

std::uint8_t AdafruitMiniI2cGamepad::readProcessorHardwareIdFromDevice() {
  return readRegister(statusModuleAddress, hardwareId, 1U).front();
}

std::uint32_t AdafruitMiniI2cGamepad::readProductIdAndFirmwareDateCodeFromDevice() {
  return decodeBigEndianBytesAsUint32(readRegister(statusModuleAddress, productAndDateCode, 4U));
}

void AdafruitMiniI2cGamepad::configureButtonInputs() {
  const std::vector<std::uint8_t> encodedButtonInputsMask = encodeUint32AsBigEndianBytes(allGamepadButtonInputsMask);

  // Clear each direction bit so all six button connections become inputs.
  writeRegister(gpioModuleAddress, gpioDirectionClear, encodedButtonInputsMask);
  // Enable a resistor on each input and configure it as a pull-up.
  writeRegister(gpioModuleAddress, gpioPullEnable, encodedButtonInputsMask);
  writeRegister(gpioModuleAddress, gpioBulkSet, encodedButtonInputsMask);
}

JoystickPosition AdafruitMiniI2cGamepad::readJoystickPosition() {
  const int rawXAxisValue = static_cast<int>(readAnalogInputValue(joystickXAxisAnalogInputNumber));
  const int rawYAxisValue = static_cast<int>(readAnalogInputValue(joystickYAxisAnalogInputNumber));

  // Reverse both axes so larger values mean right and up.
  return {
      std::clamp(joystickAxisMaximumValue - rawXAxisValue, 0, joystickAxisMaximumValue),
      std::clamp(joystickAxisMaximumValue - rawYAxisValue, 0, joystickAxisMaximumValue),
  };
}

std::uint32_t AdafruitMiniI2cGamepad::readPressedButtonMask() {
  const std::uint32_t buttonInputLevels  = readButtonInputLevels(allGamepadButtonInputsMask);
  std::uint32_t       pressedButtonsMask = 0;
  markButtonPressedIfInputIsLow(buttonInputLevels, buttonXInputNumber, GamepadButton::X, pressedButtonsMask);
  markButtonPressedIfInputIsLow(buttonInputLevels, buttonYInputNumber, GamepadButton::Y, pressedButtonsMask);
  markButtonPressedIfInputIsLow(buttonInputLevels, buttonAInputNumber, GamepadButton::A, pressedButtonsMask);
  markButtonPressedIfInputIsLow(buttonInputLevels, buttonBInputNumber, GamepadButton::B, pressedButtonsMask);
  markButtonPressedIfInputIsLow(buttonInputLevels, buttonSelectInputNumber, GamepadButton::Select, pressedButtonsMask);
  markButtonPressedIfInputIsLow(buttonInputLevels, buttonStartInputNumber, GamepadButton::Start, pressedButtonsMask);
  return pressedButtonsMask;
}

std::uint16_t AdafruitMiniI2cGamepad::readAnalogInputValue(std::uint8_t inputNumber) {
  // An ADC channel address is its input number added to the ADC offset. For
  // example, input 14 uses address 0x07 + 14 = 0x15.
  const std::uint8_t analogChannelAddress = static_cast<std::uint8_t>(adcChannelOffset + inputNumber);

  // Analog values arrive as two bytes, highest byte first. For example,
  // `{0x01, 0xff}` is decimal 511.
  const auto analogValueBytes = readRegister(adcModuleAddress, analogChannelAddress, 2U);
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(analogValueBytes[0]) << 8U) |
                                    static_cast<std::uint16_t>(analogValueBytes[1]));
}

std::uint32_t AdafruitMiniI2cGamepad::readButtonInputLevels(std::uint32_t selectedButtonInputsMask) {
  return decodeBigEndianBytesAsUint32(readRegister(gpioModuleAddress, gpioBulkRead, 4U)) & selectedButtonInputsMask;
}

void AdafruitMiniI2cGamepad::writeRegister(std::uint8_t moduleAddress, std::uint8_t registerAddress,
                                           const std::vector<std::uint8_t> &payloadBytes) {
  // A Seesaw write starts with the module and register, followed by the data.
  std::vector<std::uint8_t> i2cRequestBytes(payloadBytes.size() + 2U);
  i2cRequestBytes[0] = moduleAddress;
  i2cRequestBytes[1] = registerAddress;
  std::copy(payloadBytes.begin(), payloadBytes.end(), i2cRequestBytes.begin() + 2);
  gamepadI2cDevice_->write(i2cRequestBytes);
}

std::vector<std::uint8_t> AdafruitMiniI2cGamepad::readRegister(std::uint8_t moduleAddress, std::uint8_t registerAddress,
                                                               std::size_t numberOfBytesToRead) {
  // The board needs a short delay after the register is selected. The tested
  // Raspberry Pi program uses 8 ms.
  return gamepadI2cDevice_->writeThenRead({moduleAddress, registerAddress}, numberOfBytesToRead,
                                          std::chrono::milliseconds(8));
}

} // namespace input
} // namespace iot
