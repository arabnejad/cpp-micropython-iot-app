#include "iot/input/input_controller_base.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace iot {
namespace input {
namespace {

class TestInputController final : public InputControllerBase {
public:
  const char *modelName() const noexcept override {
    return "Test controller";
  }
  const char *boardType() const noexcept override {
    return "test_controller";
  }
  void connect() override {
    m_connected = true;
  }
  void setJoystickCentreAndDeadZoneForTest(int joystickDeadZone) {
    setJoystickCentreAndDeadZone({500, 500}, joystickDeadZone);
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

TEST(InputControllerBaseTest, ReportsEveryPressedButtonFromTheDriverMask) {
  TestInputController inputController;
  inputController.setPressedButtonMask(static_cast<std::uint32_t>(ControllerButton::A) |
                                       static_cast<std::uint32_t>(ControllerButton::Start));

  EXPECT_EQ(static_cast<std::uint32_t>(ControllerButton::A), 1U << 2U);
  EXPECT_EQ(static_cast<std::uint32_t>(ControllerButton::Start), 1U << 5U);
  EXPECT_TRUE(inputController.buttons().isPressed(ControllerButton::A));
  EXPECT_FALSE(inputController.buttons().isPressed(ControllerButton::B));
  EXPECT_EQ(inputController.buttons().pressed(),
            (std::vector<ControllerButton>{ControllerButton::A, ControllerButton::Start}));
}

TEST(InputControllerBaseTest, ReportsSeenGreatButtonNamesWithoutAdafruitAliases) {
  TestInputController inputController;
  inputController.setPressedButtonMask(static_cast<std::uint32_t>(ControllerButton::K1) |
                                       static_cast<std::uint32_t>(ControllerButton::Press));

  EXPECT_TRUE(inputController.buttons().isPressed(ControllerButton::K1));
  EXPECT_TRUE(inputController.buttons().isPressed(ControllerButton::Press));
  EXPECT_FALSE(inputController.buttons().isPressed(ControllerButton::A));
  EXPECT_FALSE(inputController.buttons().isPressed(ControllerButton::Start));
  EXPECT_EQ(inputController.buttons().pressed(),
            (std::vector<ControllerButton>{ControllerButton::K1, ControllerButton::Press}));
}

TEST(InputControllerBaseTest, UsesConfiguredCentreAndDeadZoneToCalculateDirection) {
  TestInputController inputController;
  inputController.setJoystickCentreAndDeadZoneForTest(100);
  inputController.setJoystickPosition({650, 650});

  EXPECT_EQ(inputController.joystick().direction(), JoystickDirection::UpRight);
}

TEST(InputControllerBaseTest, KeepsTheJoystickAtCenterInsideTheDeadZone) {
  TestInputController inputController;
  inputController.setJoystickCentreAndDeadZoneForTest(100);
  inputController.setJoystickPosition({600, 500});

  EXPECT_EQ(inputController.joystick().direction(), JoystickDirection::Center);
}

TEST(InputControllerBaseTest, RejectsADeadZoneOutsideTheJoystickRange) {
  TestInputController inputController;

  EXPECT_THROW(inputController.setJoystickCentreAndDeadZoneForTest(1024), std::invalid_argument);
}

TEST(InputControllerBaseTest, CalculatesEveryDirectionAndReportsTheStoredJoystickValues) {
  TestInputController inputController;
  inputController.setJoystickCentreAndDeadZoneForTest(10);

  EXPECT_EQ(inputController.joystick().centre().x, 500);
  EXPECT_EQ(inputController.joystick().centre().y, 500);
  EXPECT_EQ(inputController.joystick().deadZone(), 10);

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
    inputController.setJoystickPosition(directionTestCase.joystickPosition);
    EXPECT_EQ(inputController.joystick().position().x, directionTestCase.joystickPosition.x);
    EXPECT_EQ(inputController.joystick().position().y, directionTestCase.joystickPosition.y);
    EXPECT_EQ(inputController.joystick().x(), directionTestCase.joystickPosition.x);
    EXPECT_EQ(inputController.joystick().y(), directionTestCase.joystickPosition.y);
    EXPECT_EQ(inputController.joystick().direction(), directionTestCase.expectedDirection);
  }
}

} // namespace
} // namespace input
} // namespace iot
