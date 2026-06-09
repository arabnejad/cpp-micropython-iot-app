#include "iot/input/game_controller.h"

#include <stdexcept>

namespace iot {
namespace input {
namespace {

constexpr int maximumJoystickValue = 1023;

} // namespace

int GamepadJoystick::x() const noexcept {
  return m_x;
}

int GamepadJoystick::y() const noexcept {
  return m_y;
}

JoystickPosition GamepadJoystick::position() const noexcept {
  return {x(), y()};
}

JoystickPosition GamepadJoystick::centre() const noexcept {
  return {m_centreX, m_centreY};
}

int GamepadJoystick::deadZone() const noexcept {
  return m_deadZone;
}

JoystickDirection GamepadJoystick::direction() const noexcept {
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

void GamepadJoystick::update(JoystickPosition position) noexcept {
  m_x = position.x;
  m_y = position.y;
}

void GamepadJoystick::setCalibration(JoystickPosition centre, int deadZone) {
  if (deadZone < 0 || deadZone > maximumJoystickValue) {
    throw std::invalid_argument("Joystick dead zone must be between 0 and 1023");
  }
  m_centreX  = centre.x;
  m_centreY  = centre.y;
  m_deadZone = deadZone;
}

bool GamepadButtons::isPressed(GamepadButton button) const noexcept {
  return (m_pressedMask & static_cast<std::uint32_t>(button)) != 0U;
}

std::vector<GamepadButton> GamepadButtons::pressed() const {
  static constexpr GamepadButton allButtons[]{
      GamepadButton::X, GamepadButton::Y,      GamepadButton::A,
      GamepadButton::B, GamepadButton::Select, GamepadButton::Start,
  };

  std::vector<GamepadButton> pressedButtons;
  const std::uint32_t        currentMask = m_pressedMask;
  for (GamepadButton button : allButtons) {
    if ((currentMask & static_cast<std::uint32_t>(button)) != 0U) {
      pressedButtons.push_back(button);
    }
  }
  return pressedButtons;
}

void GamepadButtons::update(std::uint32_t pressedMask) noexcept {
  m_pressedMask = pressedMask;
}

const GamepadJoystick &GameController::joystick() const noexcept {
  return m_joystick;
}

const GamepadButtons &GameController::buttons() const noexcept {
  return m_buttons;
}

void GameController::updateJoystick(JoystickPosition position) noexcept {
  m_joystick.update(position);
}

void GameController::setJoystickCalibration(JoystickPosition centre, int deadZone) {
  m_joystick.setCalibration(centre, deadZone);
}

void GameController::updateButtons(std::uint32_t pressedMask) noexcept {
  m_buttons.update(pressedMask);
}

} // namespace input
} // namespace iot
