#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace iot {
namespace ui {

/*
 * RGB888 gives each pixel three colour values:
 *
 *   Red       Green     Blue
 *   8 bits    8 bits    8 bits
 *
 * This uses 24 bits, or 3 bytes, for each pixel. LVGL keeps those bytes in
 * blue, green, red order in memory, which is why the pixel vector is named
 * bgrPixelBytes.
 */
struct DecodedJpegImage {
  std::filesystem::path     sourceFilePath;
  std::uint32_t             width{0};
  std::uint32_t             height{0};
  std::uint32_t             rowStrideInBytes{0};
  std::vector<std::uint8_t> bgrPixelBytes;
};

/* Decoded pixels and the position where LVGL should draw them. */
struct DecodedJpegImageSpec {
  std::shared_ptr<const DecodedJpegImage> decodedImage;
  std::int32_t                            x{0};
  std::int32_t                            y{0};
};

/* Decoded pixels placed behind the other screen widgets. */
struct DecodedBackgroundJpegImageSpec {
  std::shared_ptr<const DecodedJpegImage> decodedImage;
  bool                                    repeatImageAsTiles{false};
};

} // namespace ui
} // namespace iot
