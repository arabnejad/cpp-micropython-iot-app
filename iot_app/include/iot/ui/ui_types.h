#pragma once

#include <cstdint>
#include <string>

namespace iot {
namespace ui {

/* ID used by Python to update or delete a widget. */
using WidgetId = std::uint64_t;

/* Red, green, and blue values from 0 to 255. */
struct Color {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

/* Pixel position and size on the screen. */
struct Rect {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

/* Values used when a text box is created. */
struct TextBoxSpec {
  Rect        bounds;
  std::string text;
  Color       textColor{255, 255, 255};
  Color       backgroundColor{0, 0, 0};
  Color       borderColor{255, 255, 255};
  /* Zero is transparent and 255 is fully solid. */
  std::uint8_t backgroundOpacity{0};
  /* Border thickness in pixels. Zero hides the border. */
  std::uint16_t borderWidth{0};
  std::uint16_t fontSize{24};
};

/* Position, size, and colour of a solid rectangle. */
struct FilledAreaSpec {
  Rect  bounds;
  Color color;
};

} // namespace ui
} // namespace iot
