#pragma once

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#else
#include <stdint.h>
#endif

/*
 * libjpeg-turbo can reduce a JPEG to one eighth of its original size. One
 * eighth is 12.5%, but the Python API accepts only whole percentages, so the
 * smallest accepted value is 13. A value of 100 keeps the original size, and
 * values above 100 are rejected because IoT App does not enlarge images.
 */
enum {
  IOT_MINIMUM_JPEG_SCALE_PERCENT = 13,
  IOT_MAXIMUM_JPEG_SCALE_PERCENT = 100,
};

#ifdef __cplusplus
namespace iot {
namespace ui {

constexpr std::uint16_t minimumJpegScalePercent = IOT_MINIMUM_JPEG_SCALE_PERCENT;
constexpr std::uint16_t maximumJpegScalePercent = IOT_MAXIMUM_JPEG_SCALE_PERCENT;

/* Limits that protect memory use while a JPEG is read and decoded. */
constexpr std::size_t   maximumCompressedJpegFileSizeInBytes = 10U * 1024U * 1024U;
constexpr std::size_t   maximumDecodedJpegPixelBytes         = 32U * 1024U * 1024U;
constexpr std::size_t   decodedJpegCacheCapacityInBytes      = 32U * 1024U * 1024U;
constexpr std::uint32_t maximumJpegDimensionInPixels         = 8192U;
constexpr std::uint64_t maximumJpegPixelCount                = 16U * 1024U * 1024U;

} // namespace ui
} // namespace iot
#endif
