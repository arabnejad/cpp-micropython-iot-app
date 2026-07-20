#pragma once

#include "iot/ui/decoded_jpeg_image.h"
#include "iot/ui/jpeg_limits.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace iot {
namespace ui {
namespace internal {

/* Size limits used when one JPEG is decoded. Zero width or height means no limit. */
struct JpegDecodeRequest {
  std::filesystem::path sourceFilePath;
  std::uint32_t         maximumWidth{0};
  std::uint32_t         maximumHeight{0};
  std::uint16_t         maximumScalePercent{maximumJpegScalePercent};
};

/*
 * Converts one JPEG file into pixels without calling LVGL. Keeping this small
 * interface separate lets decoder and cache tests use a predictable decoder.
 */
class IJpegImageDecoder {
public:
  virtual ~IJpegImageDecoder() = default;

  IJpegImageDecoder(const IJpegImageDecoder &)            = delete;
  IJpegImageDecoder &operator=(const IJpegImageDecoder &) = delete;
  IJpegImageDecoder(IJpegImageDecoder &&)                 = delete;
  IJpegImageDecoder &operator=(IJpegImageDecoder &&)      = delete;

  /* Decodes the largest JPEG size allowed by the request and safety limits. */
  virtual std::shared_ptr<const DecodedJpegImage> decode(const JpegDecodeRequest &decodeRequest) = 0;

protected:
  IJpegImageDecoder() = default;
};

/* Creates the libjpeg-turbo decoder used by ScreenManager. */
std::unique_ptr<IJpegImageDecoder> makeLibjpegTurboJpegImageDecoder();

} // namespace internal
} // namespace ui
} // namespace iot
