#include "iot/input/input_controller_base.h"

#include <stdexcept>

namespace iot {
namespace input {
namespace {

constexpr int maximumJoystickValue = 1023;

} // namespace

int ControllerJoystick::x() const noexcept {
  return m_x;
}

int ControllerJoystick::y() const noexcept {
  return m_y;
}

JoystickPosition ControllerJoystick::position() const noexcept {
  return {x(), y()};
}

JoystickPosition ControllerJoystick::centre() const noexcept {
  return {m_centreX, m_centreY};
}

int ControllerJoystick::deadZone() const noexcept {
  return m_deadZone;
}

JoystickDirection ControllerJoystick::direction() const noexcept {
  const JoystickPosition currentJoystickPosition  = position();
  const JoystickPosition calibratedJoystickCentre = centre();
  const int              joystickDeadZone         = deadZone();

  const bool left  = currentJoystickPosition.x < calibratedJoystickCentre.x - joystickDeadZone;
  const bool right = currentJoystickPosition.x > calibratedJoystickCentre.x + joystickDeadZone;
  const bool down  = currentJoystickPosition.y < calibratedJoystickCentre.y - joystickDeadZone;
  const bool up    = currentJoystickPosition.y > calibratedJoystickCentre.y + joystickDeadZone;

  if (up && left) {
    return JoystickDirection::UpLeft;
  }
  if (up && right) {
    return JoystickDirection::UpRight;
  }
  if (down && left) {
    return JoystickDirection::DownLeft;
  }
  if (down && right) {
    return JoystickDirection::DownRight;
  }
  if (up) {
    return JoystickDirection::Up;
  }
  if (down) {
    return JoystickDirection::Down;
  }
  if (left) {
    return JoystickDirection::Left;
  }
  if (right) {
    return JoystickDirection::Right;
  }
  return JoystickDirection::Center;
}

void ControllerJoystick::update(JoystickPosition position) noexcept {
  m_x = position.x;
  m_y = position.y;
}

void ControllerJoystick::setCentreAndDeadZone(JoystickPosition centre, int deadZone) {
  if (deadZone < 0 || deadZone > maximumJoystickValue) {
    throw std::invalid_argument("Joystick dead zone must be between 0 and 1023");
  }
  m_centreX  = centre.x;
  m_centreY  = centre.y;
  m_deadZone = deadZone;
}

bool ControllerButtons::isPressed(ControllerButton button) const noexcept {
  return (m_pressedMask & static_cast<std::uint32_t>(button)) != 0U;
}

std::vector<ControllerButton> ControllerButtons::pressed() const {
  static constexpr ControllerButton allButtons[]{
      ControllerButton::X,      ControllerButton::Y,     ControllerButton::A,  ControllerButton::B,
      ControllerButton::Select, ControllerButton::Start, ControllerButton::K1, ControllerButton::K2,
      ControllerButton::K3,     ControllerButton::Press,
  };

  std::vector<ControllerButton> pressedButtons;
  const std::uint32_t           currentMask = m_pressedMask;
  for (ControllerButton button : allButtons) {
    if ((currentMask & static_cast<std::uint32_t>(button)) != 0U) {
      pressedButtons.push_back(button);
    }
  }
  return pressedButtons;
}

void ControllerButtons::update(std::uint32_t pressedMask) noexcept {
  m_pressedMask = pressedMask;
}

const ControllerJoystick &InputControllerBase::joystick() const noexcept {
  return m_joystick;
}

const ControllerButtons &InputControllerBase::buttons() const noexcept {
  return m_buttons;
}

void InputControllerBase::updateJoystick(JoystickPosition position) noexcept {
  m_joystick.update(position);
}

void InputControllerBase::setJoystickCentreAndDeadZone(JoystickPosition centre, int deadZone) {
  m_joystick.setCentreAndDeadZone(centre, deadZone);
}

void InputControllerBase::updateButtons(std::uint32_t pressedMask) noexcept {
  m_buttons.update(pressedMask);
}

} // namespace input
} // namespace iot
