#include "iot/input/game_controller.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace iot {
namespace input {
namespace {

class TestGameController final : public GameController {
public:
  const char *modelName() const noexcept override {
    return "Test gamepad";
  }
  void connect() override {
    m_connected = true;
  }
  void calibrateJoystick(std::size_t, int joystickDeadZone) override {
    setJoystickCalibration({500, 500}, joystickDeadZone);
  }
  void refreshInputState() override {}
  bool isConnected() const noexcept override {
    return m_connected;
  }

  void setJoystickPosition(JoystickPosition joystickPosition) {
    updateJoystick(joystickPosition);
  }
  void setPressedButtonMask(std::uint32_t pressedButtonMask) {
    updateButtons(pressedButtonMask);
  }

private:
  bool m_connected{false};
};

TEST(GameControllerTest, ReportsEveryPressedButtonFromTheDriverMask) {
  TestGameController gameController;
  gameController.setPressedButtonMask(static_cast<std::uint32_t>(GamepadButton::A) |
                                      static_cast<std::uint32_t>(GamepadButton::Start));

  EXPECT_EQ(static_cast<std::uint32_t>(GamepadButton::A), 1U << 2U);
  EXPECT_EQ(static_cast<std::uint32_t>(GamepadButton::Start), 1U << 5U);
  EXPECT_TRUE(gameController.buttons().isPressed(GamepadButton::A));
  EXPECT_FALSE(gameController.buttons().isPressed(GamepadButton::B));
  EXPECT_EQ(gameController.buttons().pressed(), (std::vector<GamepadButton>{GamepadButton::A, GamepadButton::Start}));
}

TEST(GameControllerTest, UsesCalibrationAndDeadZoneToCalculateDirection) {
  TestGameController gameController;
  gameController.calibrateJoystick(1U, 100);
  gameController.setJoystickPosition({650, 650});

  EXPECT_EQ(gameController.joystick().direction(), JoystickDirection::UpRight);
}

TEST(GameControllerTest, KeepsTheJoystickAtCenterInsideTheDeadZone) {
  TestGameController gameController;
  gameController.calibrateJoystick(1U, 100);
  gameController.setJoystickPosition({600, 500});

  EXPECT_EQ(gameController.joystick().direction(), JoystickDirection::Center);
}

TEST(GameControllerTest, RejectsADeadZoneOutsideTheJoystickRange) {
  TestGameController gameController;

  EXPECT_THROW(gameController.calibrateJoystick(1U, 1024), std::invalid_argument);
}

TEST(GameControllerTest, CalculatesEveryDirectionAndReportsTheStoredJoystickValues) {
  TestGameController gameController;
  gameController.calibrateJoystick(1U, 10);

  EXPECT_EQ(gameController.joystick().centre().x, 500);
  EXPECT_EQ(gameController.joystick().centre().y, 500);
  EXPECT_EQ(gameController.joystick().deadZone(), 10);

  struct DirectionTestCase {
    JoystickPosition  joystickPosition;
    JoystickDirection expectedDirection;
  };
  const std::vector<DirectionTestCase> directionTestCases{
      {{500, 511}, JoystickDirection::Up},       {{500, 489}, JoystickDirection::Down},
      {{489, 500}, JoystickDirection::Left},     {{511, 500}, JoystickDirection::Right},
      {{489, 511}, JoystickDirection::UpLeft},   {{511, 511}, JoystickDirection::UpRight},
      {{489, 489}, JoystickDirection::DownLeft}, {{511, 489}, JoystickDirection::DownRight},
  };
  for (const auto &directionTestCase : directionTestCases) {
    gameController.setJoystickPosition(directionTestCase.joystickPosition);
    EXPECT_EQ(gameController.joystick().position().x, directionTestCase.joystickPosition.x);
    EXPECT_EQ(gameController.joystick().position().y, directionTestCase.joystickPosition.y);
    EXPECT_EQ(gameController.joystick().x(), directionTestCase.joystickPosition.x);
    EXPECT_EQ(gameController.joystick().y(), directionTestCase.joystickPosition.y);
    EXPECT_EQ(gameController.joystick().direction(), directionTestCase.expectedDirection);
  }
}

} // namespace
} // namespace input
} // namespace iot
