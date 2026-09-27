#pragma once

#include <cstdint>
#include <vector>

namespace iot {
namespace input {

/*
 * Button names from the supported controllers.
 *
 * Each value is a bit in the application's pressed-button mask. These are not
 * the physical input numbers wired on a particular board.
 *
 * For example, the Adafruit board's A button is connected to physical input 5.
 * ControllerButton::A = 1U << 2U means the application stores that button in bit 2
 * of its pressed-button mask.
 * The SeenGreat HAT has K1, K2, K3 and a pressable joystick. Those are
 * separate buttons, not aliases for the Adafruit board's A/B/X/Start buttons.
 */
enum class ControllerButton : std::uint32_t {
  X      = 1U << 0U,
  Y      = 1U << 1U,
  A      = 1U << 2U,
  B      = 1U << 3U,
  Select = 1U << 4U,
  Start  = 1U << 5U,
  K1     = 1U << 6U,
  K2     = 1U << 7U,
  K3     = 1U << 8U,
  Press  = 1U << 9U,
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
class ControllerJoystick {
public:
  int              x() const noexcept;
  int              y() const noexcept;
  JoystickPosition position() const noexcept;
  /* Gets the centre measured or fixed by the board's driver. */
  JoystickPosition centre() const noexcept;
  /* Gets distance that the stick must move before a direction is reported. */
  int deadZone() const noexcept;
  /* Calculates the direction from the latest position, centre, and dead zone. */
  JoystickDirection direction() const noexcept;

private:
  friend class InputControllerBase;

  void update(JoystickPosition position) noexcept;
  void setCentreAndDeadZone(JoystickPosition centre, int deadZone);

  int m_x{0};
  int m_y{0};
  int m_centreX{512};
  int m_centreY{512};
  int m_deadZone{100};
};

/* Keeps the latest pressed/released state of the controller buttons. */
class ControllerButtons {
public:
  /* Checks whether one button is held down. */
  bool isPressed(ControllerButton button) const noexcept;
  /* All buttons currently held down. */
  std::vector<ControllerButton> pressed() const;

private:
  friend class InputControllerBase;

  void update(std::uint32_t pressedMask) noexcept;

  std::uint32_t m_pressedMask{0};
};

/*
 * Shared input-controller base, independent of its connection type.
 *
 * Adafruit reads inputs over I2C, while SeenGreat reads them through GPIO.
 * Both drivers update the joystick and button values stored here.
 *
 * Use a controller object from one thread at a time. If several threads need
 * it, the caller must provide the locking.
 */
class InputControllerBase {
public:
  virtual ~InputControllerBase() = default;

  // Represents one physical controller; copying and moving are disabled.
  InputControllerBase(const InputControllerBase &)            = delete;
  InputControllerBase &operator=(const InputControllerBase &) = delete;
  InputControllerBase(InputControllerBase &&)                 = delete;
  InputControllerBase &operator=(InputControllerBase &&)      = delete;

  virtual const char *modelName() const noexcept = 0;
  /* Stable board name for applications that need particular physical buttons. */
  virtual const char *boardType() const noexcept = 0;
  /* Connects to the device and prepares its inputs. */
  virtual void connect() = 0;
  /* Reads the current joystick and buttons from the device. */
  virtual void refreshInputState() = 0;
  /* Checks whether connect() completed successfully. */
  virtual bool isConnected() const noexcept = 0;

  const ControllerJoystick &joystick() const noexcept;
  const ControllerButtons  &buttons() const noexcept;

protected:
  InputControllerBase() = default;

  /* Stores a joystick position read by the hardware driver. */
  void updateJoystick(JoystickPosition position) noexcept;
  /* Stores the centre and dead zone used to calculate directions. */
  void setJoystickCentreAndDeadZone(JoystickPosition centre, int deadZone);
  /* Stores the pressed-button mask read by the hardware driver. */
  void updateButtons(std::uint32_t pressedMask) noexcept;

private:
  ControllerJoystick m_joystick;
  ControllerButtons  m_buttons;
};

} // namespace input
} // namespace iot
