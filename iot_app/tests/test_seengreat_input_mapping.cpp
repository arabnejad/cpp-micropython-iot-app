#include "input/seengreat_input_mapping.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using iot::input::ControllerButton;
using iot::input::internal::mapSeenGreatPressedKeys;
using iot::input::internal::SeenGreatPressedKeys;

std::uint32_t buttonMask(ControllerButton button) {
  return static_cast<std::uint32_t>(button);
}

TEST(SeenGreatInputMappingTest, ReturnsCentreAndNoButtonsWhenEveryKeyIsReleased) {
  const auto mappedInput = mapSeenGreatPressedKeys(SeenGreatPressedKeys{});

  EXPECT_EQ(mappedInput.joystickPosition.x, 512);
  EXPECT_EQ(mappedInput.joystickPosition.y, 512);
  EXPECT_EQ(mappedInput.pressedButtonsMask, 0U);
}

TEST(SeenGreatInputMappingTest, MapsEachDirectionToTheCorrectJoystickAxis) {
  SeenGreatPressedKeys pressedKeys;

  pressedKeys.up = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).joystickPosition.y, 1023);
  pressedKeys.up = false;

  pressedKeys.down = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).joystickPosition.y, 0);
  pressedKeys.down = false;

  pressedKeys.left = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).joystickPosition.x, 0);
  pressedKeys.left = false;

  pressedKeys.right = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).joystickPosition.x, 1023);
}

TEST(SeenGreatInputMappingTest, CombinesTwoDirectionsIntoADiagonal) {
  SeenGreatPressedKeys pressedKeys;
  pressedKeys.up   = true;
  pressedKeys.left = true;

  const auto mappedInput = mapSeenGreatPressedKeys(pressedKeys);
  EXPECT_EQ(mappedInput.joystickPosition.x, 0);
  EXPECT_EQ(mappedInput.joystickPosition.y, 1023);
}

TEST(SeenGreatInputMappingTest, OppositeDirectionsCancelEachOther) {
  SeenGreatPressedKeys pressedKeys;
  pressedKeys.up    = true;
  pressedKeys.down  = true;
  pressedKeys.left  = true;
  pressedKeys.right = true;

  const auto mappedInput = mapSeenGreatPressedKeys(pressedKeys);
  EXPECT_EQ(mappedInput.joystickPosition.x, 512);
  EXPECT_EQ(mappedInput.joystickPosition.y, 512);
}

TEST(SeenGreatInputMappingTest, KeepsTheThreeKeysAndJoystickPressDistinctFromAdafruitButtons) {
  SeenGreatPressedKeys pressedKeys;
  pressedKeys.key1 = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).pressedButtonsMask, buttonMask(ControllerButton::K1));

  pressedKeys.key1 = false;
  pressedKeys.key2 = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).pressedButtonsMask, buttonMask(ControllerButton::K2));

  pressedKeys.key2 = false;
  pressedKeys.key3 = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).pressedButtonsMask, buttonMask(ControllerButton::K3));

  pressedKeys.key3   = false;
  pressedKeys.centre = true;
  EXPECT_EQ(mapSeenGreatPressedKeys(pressedKeys).pressedButtonsMask, buttonMask(ControllerButton::Press));
}

TEST(SeenGreatInputMappingTest, CombinesSeenGreatButtonsWithoutReportingAdafruitButtons) {
  SeenGreatPressedKeys pressedKeys;
  pressedKeys.key1   = true;
  pressedKeys.key2   = true;
  pressedKeys.key3   = true;
  pressedKeys.centre = true;

  const auto mappedInput     = mapSeenGreatPressedKeys(pressedKeys);
  const auto expectedButtons = buttonMask(ControllerButton::K1) | buttonMask(ControllerButton::K2) |
                               buttonMask(ControllerButton::K3) | buttonMask(ControllerButton::Press);
  EXPECT_EQ(mappedInput.pressedButtonsMask, expectedButtons);
  EXPECT_EQ(mappedInput.pressedButtonsMask & buttonMask(ControllerButton::Y), 0U);
  EXPECT_EQ(mappedInput.pressedButtonsMask & buttonMask(ControllerButton::Select), 0U);
  EXPECT_EQ(mappedInput.pressedButtonsMask & buttonMask(ControllerButton::A), 0U);
  EXPECT_EQ(mappedInput.pressedButtonsMask & buttonMask(ControllerButton::Start), 0U);
}

} // namespace
