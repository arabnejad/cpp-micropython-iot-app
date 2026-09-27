#include "seengreat_input_mapping.h"

#include <array>

namespace iot {
namespace input {
namespace internal {

SeenGreatMappedInput mapSeenGreatPressedKeys(const SeenGreatPressedKeys &pressedKeys) noexcept {
  constexpr int centrePosition = 512;
  constexpr int lowPosition    = 0;
  constexpr int highPosition   = 1023;

  SeenGreatMappedInput mappedInput;
  mappedInput.joystickPosition.x =
      pressedKeys.left == pressedKeys.right ? centrePosition : (pressedKeys.left ? lowPosition : highPosition);
  mappedInput.joystickPosition.y =
      pressedKeys.up == pressedKeys.down ? centrePosition : (pressedKeys.up ? highPosition : lowPosition);

  struct ButtonState {
    bool             pressed;
    ControllerButton button;
  };
  const std::array<ButtonState, 4> buttons{{
      {pressedKeys.key1, ControllerButton::K1},
      {pressedKeys.key2, ControllerButton::K2},
      {pressedKeys.key3, ControllerButton::K3},
      {pressedKeys.centre, ControllerButton::Press},
  }};
  for (const ButtonState &button : buttons) {
    if (button.pressed) {
      mappedInput.pressedButtonsMask |= static_cast<std::uint32_t>(button.button);
    }
  }
  return mappedInput;
}

} // namespace internal
} // namespace input
} // namespace iot
