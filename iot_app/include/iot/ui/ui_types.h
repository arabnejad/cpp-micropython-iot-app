#pragma once

#include "iot/ui/jpeg_limits.h"

#include <cstdint>
#include <filesystem>
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

/* Controls how a JPEG is placed behind the other screen content. */
enum class BackgroundImageMode {
  /* Keep the decoded size, centre it, and crop anything outside the screen. */
  Center,
  /* Reduce a large image until all of it fits. Small images stay unchanged. */
  Fit,
  /* Repeat the image across the screen. */
  Tile,
};

/* File, position, and scale used for a normal JPEG image widget. */
struct JpegImageSpec {
  std::filesystem::path filePath;
  std::int32_t          x{0};
  std::int32_t          y{0};
  /* 100 keeps the original size. A smaller value reduces the image. */
  std::uint16_t scalePercent{maximumJpegScalePercent};
};

/* File and placement used for a JPEG behind all normal widgets. */
struct BackgroundJpegImageSpec {
  std::filesystem::path filePath;
  BackgroundImageMode   mode{BackgroundImageMode::Center};
  /* Fit may reduce the image further when it is larger than the screen. */
  std::uint16_t scalePercent{maximumJpegScalePercent};
};

} // namespace ui
} // namespace iot
