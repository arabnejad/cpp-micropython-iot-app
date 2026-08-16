#include "iot/input/game_controller.h"

#include <stdexcept>

namespace iot {
namespace input {
namespace {

constexpr int maximumJoystickValue = 1023;

} // namespace

int GamepadJoystick::x() const noexcept {
  return x_;
}

int GamepadJoystick::y() const noexcept {
  return y_;
}

JoystickPosition GamepadJoystick::position() const noexcept {
  return {x(), y()};
}

JoystickPosition GamepadJoystick::centre() const noexcept {
  return {centreX_, centreY_};
}

int GamepadJoystick::deadZone() const noexcept {
  return deadZone_;
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
  x_ = position.x;
  y_ = position.y;
}

void GamepadJoystick::setCalibration(JoystickPosition centre, int deadZone) {
  if (deadZone < 0 || deadZone > maximumJoystickValue) {
    throw std::invalid_argument("Joystick dead zone must be between 0 and 1023");
  }
  centreX_  = centre.x;
  centreY_  = centre.y;
  deadZone_ = deadZone;
}

bool GamepadButtons::isPressed(GamepadButton button) const noexcept {
  return (pressedMask_ & static_cast<std::uint32_t>(button)) != 0U;
}

std::vector<GamepadButton> GamepadButtons::pressed() const {
  static constexpr GamepadButton allButtons[]{
      GamepadButton::X, GamepadButton::Y,      GamepadButton::A,
      GamepadButton::B, GamepadButton::Select, GamepadButton::Start,
  };

  std::vector<GamepadButton> pressedButtons;
  const std::uint32_t        currentMask = pressedMask_;
  for (GamepadButton button : allButtons) {
    if ((currentMask & static_cast<std::uint32_t>(button)) != 0U) {
      pressedButtons.push_back(button);
    }
  }
  return pressedButtons;
}

void GamepadButtons::update(std::uint32_t pressedMask) noexcept {
  pressedMask_ = pressedMask;
}

const GamepadJoystick &GameController::joystick() const noexcept {
  return joystick_;
}

const GamepadButtons &GameController::buttons() const noexcept {
  return buttons_;
}

void GameController::updateJoystick(JoystickPosition position) noexcept {
  joystick_.update(position);
}

void GameController::setJoystickCalibration(JoystickPosition centre, int deadZone) {
  joystick_.setCalibration(centre, deadZone);
}

void GameController::updateButtons(std::uint32_t pressedMask) noexcept {
  buttons_.update(pressedMask);
}

} // namespace input
} // namespace iot
