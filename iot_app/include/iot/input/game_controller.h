#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace iot {
namespace input {

/*
 * Buttons that every supported game controller can report.
 *
 * Each value is a bit in the application's pressed-button mask. These are not
 * the physical input numbers wired on a particular board.
 *
 * For example, buttonAInputNumber = 5 means the Adafruit board's A button is
 * physically connected to input 5. GamepadButton::A = 1U << 2U means the
 * application stores the A button in bit 2 of its pressed-button mask.
 */
enum class GamepadButton : std::uint32_t {
  X      = 1U << 0U,
  Y      = 1U << 1U,
  A      = 1U << 2U,
  B      = 1U << 3U,
  Select = 1U << 4U,
  Start  = 1U << 5U,
};

/* Direction of the joystick after its centre and dead zone are applied. */
enum class JoystickDirection {
  Center,
  Left,
  Right,
  Up,
  Down,
  UpLeft,
  UpRight,
  DownLeft,
  DownRight,
};

/* Horizontal and vertical joystick values, each from 0 to 1023. */
struct JoystickPosition {
  int x{0};
  int y{0};
};

/* Keeps the latest joystick reading and turns it into a direction. */
class GamepadJoystick {
public:
  int              x() const noexcept;
  int              y() const noexcept;
  JoystickPosition position() const noexcept;
  /* Gets the centre measured during the last calibration. */
  JoystickPosition centre() const noexcept;
  /* Gets distance that the stick must move before a direction is reported. */
  int deadZone() const noexcept;
  /* Calculates the direction from the latest position and calibration. */
  JoystickDirection direction() const noexcept;

private:
  friend class GameController;

  void update(JoystickPosition position) noexcept;
  void setCalibration(JoystickPosition centre, int deadZone);

  int m_x{0};
  int m_y{0};
  int m_centreX{512};
  int m_centreY{512};
  int m_deadZone{100};
};

/* Keeps the latest pressed/released state of all gamepad buttons. */
class GamepadButtons {
public:
  /* Checks whether one button is held down. */
  bool isPressed(GamepadButton button) const noexcept;
  /* All buttons currently held down. */
  std::vector<GamepadButton> pressed() const;

private:
  friend class GameController;

  void update(std::uint32_t pressedMask) noexcept;

  std::uint32_t m_pressedMask{0};
};

/*
 * Common game-controller interface, independent of its connection type.
 *
 * The current driver uses I2C. A future USB or GPIO driver can implement the
 * same interface without changing application code. Each driver updates the
 * joystick and button values stored here.
 *
 * Use a controller object from one thread at a time. If several threads need
 * it, the caller must provide the locking.
 */
class GameController {
public:
  virtual ~GameController() = default;

  // Represents one physical controller; copying and moving are disabled.
  GameController(const GameController &)            = delete;
  GameController &operator=(const GameController &) = delete;
  GameController(GameController &&)                 = delete;
  GameController &operator=(GameController &&)      = delete;

  virtual const char *modelName() const noexcept = 0;
  /* Connects to the device and prepares its inputs. */
  virtual void connect() = 0;
  /* Measures the joystick position while it is at rest. */
  virtual void calibrateJoystick(std::size_t numberOfCalibrationSamples = 20U, int joystickDeadZone = 100) = 0;
  /* Reads the current joystick and buttons from the device. */
  virtual void refreshInputState() = 0;
  /* Checks whether connect() completed successfully. */
  virtual bool isConnected() const noexcept = 0;

  const GamepadJoystick &joystick() const noexcept;
  const GamepadButtons  &buttons() const noexcept;

protected:
  GameController() = default;

  /* Stores a joystick position read by the hardware driver. */
  void updateJoystick(JoystickPosition position) noexcept;
  /* Stores the centre and dead zone measured during calibration. */
  void setJoystickCalibration(JoystickPosition centre, int deadZone);
  /* Stores the pressed-button mask read by the hardware driver. */
  void updateButtons(std::uint32_t pressedMask) noexcept;

private:
  GamepadJoystick m_joystick;
  GamepadButtons  m_buttons;
};

} // namespace input
} // namespace iot
