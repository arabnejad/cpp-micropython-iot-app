#pragma once

#include "iot/hardware/i2c_device.h"
#include "iot/input/game_controller.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace iot {
namespace input {

/*
 * Driver for the Adafruit Mini I2C STEMMA QT Gamepad (product 5743).
 *
 * This driver contains the Seesaw registers and input mapping for this board.
 * Code that only needs buttons and joystick directions can use GameController.
 */
class AdafruitMiniI2cGamepad : public GameController {
public:
  /* Opens the gamepad through Linux I2C. */
  AdafruitMiniI2cGamepad(int i2cBusNumber, std::uint8_t i2cAddress);
  /* Uses the supplied I2C implementation. */
  AdafruitMiniI2cGamepad(std::unique_ptr<hardware::II2cDevice> gamepadI2cDevice);
  ~AdafruitMiniI2cGamepad() override = default;

  // Owns one gamepad connection; copying and moving are disabled.
  AdafruitMiniI2cGamepad(const AdafruitMiniI2cGamepad &)            = delete;
  AdafruitMiniI2cGamepad &operator=(const AdafruitMiniI2cGamepad &) = delete;
  AdafruitMiniI2cGamepad(AdafruitMiniI2cGamepad &&)                 = delete;
  AdafruitMiniI2cGamepad &operator=(AdafruitMiniI2cGamepad &&)      = delete;

  const char *modelName() const noexcept override;

  /*
   * Resets the board, checks that it is product 5743, and prepares its buttons.
   */
  void connect() override;

  /* Measures the joystick centre while the user leaves it untouched. */
  void calibrateJoystick(std::size_t numberOfCalibrationSamples = 20U, int joystickDeadZone = 100) override;

  /* Reads the joystick and buttons and stores their latest values. */
  void refreshInputState() override;

  /* True after connect() has completed successfully. */
  bool isConnected() const noexcept override;

  /* Gets the Linux I2C bus number used by this gamepad. */
  int i2cBusNumber() const noexcept;
  /* Gets the seven-bit I2C address used by this gamepad. */
  std::uint8_t i2cAddress() const noexcept;
  /* Gets the Linux I2C device path, such as /dev/i2c-1. */
  const std::string &i2cDevicePath() const noexcept;

  /* Gets the processor ID reported by the board. */
  std::uint8_t processorHardwareId() const noexcept;
  /* Gets the raw 32-bit value containing the product ID and firmware date. */
  std::uint32_t productIdAndFirmwareDateCode() const noexcept;
  /* Gets the product ID stored in the upper 16 bits. */
  std::uint16_t firmwareProductId() const noexcept;
  /* Gets the encoded firmware date stored in the lower 16 bits. */
  std::uint16_t firmwareDateCode() const noexcept;

private:
  /* Throws a clear error when an operation is called before connect(). */
  void throwIfGamepadIsNotConnected() const;
  /* Restarts the gamepad processor and waits 500 ms for it to become ready. */
  void resetGamepadProcessor();
  /* Reads the one-byte processor ID. */
  std::uint8_t readProcessorHardwareIdFromDevice();
  /* Reads the four-byte product ID and firmware date value. */
  std::uint32_t readProductIdAndFirmwareDateCodeFromDevice();
  /* Configures all six button inputs with pull-up resistors. */
  void configureButtonInputs();
  /* Reads both joystick axes and makes right/up increase the values. */
  JoystickPosition readJoystickPosition();
  /* Reads the active-low inputs and builds the pressed-button mask. */
  std::uint32_t readPressedButtonMask();
  /* Reads one analog input from the gamepad processor. */
  std::uint16_t readAnalogInputValue(std::uint8_t inputNumber);
  /* Reads whether each selected button input is high or low. */
  std::uint32_t readButtonInputLevels(std::uint32_t selectedButtonInputsMask);
  /* Writes bytes to a Seesaw module and register. */
  void writeRegister(std::uint8_t moduleAddress, std::uint8_t registerAddress,
                     const std::vector<std::uint8_t> &payloadBytes);
  /* Reads bytes from a Seesaw module and register. */
  std::vector<std::uint8_t> readRegister(std::uint8_t moduleAddress, std::uint8_t registerAddress,
                                         std::size_t numberOfBytesToRead);

  std::unique_ptr<hardware::II2cDevice> m_gamepadI2cDevice;
  bool                                  m_isConnected{false};
  std::uint8_t                          m_processorHardwareId{0};
  std::uint32_t                         m_productIdAndFirmwareDateCode{0};
};

} // namespace input
} // namespace iot
