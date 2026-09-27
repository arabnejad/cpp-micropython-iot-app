#pragma once

#include "iot/input/input_controller_base.h"

#include <cstdint>

namespace iot {
namespace input {
namespace internal {

/* The eight keys read from the SeenGreat HAT's GPIO lines. */
struct SeenGreatPressedKeys {
  bool up{false};
  bool down{false};
  bool left{false};
  bool right{false};
  bool centre{false};
  bool key1{false};
  bool key2{false};
  bool key3{false};
};

struct SeenGreatMappedInput {
  JoystickPosition joystickPosition;
  std::uint32_t    pressedButtonsMask{0U};
};

SeenGreatMappedInput mapSeenGreatPressedKeys(const SeenGreatPressedKeys &pressedKeys) noexcept;

} // namespace internal
} // namespace input
} // namespace iot
