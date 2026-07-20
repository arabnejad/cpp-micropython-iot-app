#include "internal/ijpeg_image_decoder.h"

// The tj-prefixed functions and TJSCALED macro come from TurboJPEG.
// Their parameters and return values are documented in the upstream header:
// https://github.com/libjpeg-turbo/libjpeg-turbo/blob/2.1.5.1/turbojpeg.h
#include <turbojpeg.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace iot {
namespace ui {
namespace internal {
namespace {

/* Keeps the library's decoder alive until this object leaves its scope. */
class TurboJpegHandle {
public:
  // tjInitDecompress creates a decoder and returns its handle, or null on
  // failure. It does not read a file or decode any pixels yet.
  TurboJpegHandle() : m_handle(tjInitDecompress()) {
    if (m_handle == nullptr) {
      throw std::runtime_error("libjpeg-turbo could not create a JPEG decoder");
    }
  }

  ~TurboJpegHandle() {
    if (m_handle != nullptr) {
      // tjDestroy releases the decoder's internal memory. Our decoded pixel
      // vector is separate and remains owned by DecodedJpegImage.
      tjDestroy(m_handle);
    }
  }

  TurboJpegHandle(const TurboJpegHandle &)            = delete;
  TurboJpegHandle &operator=(const TurboJpegHandle &) = delete;
  TurboJpegHandle(TurboJpegHandle &&)                 = delete;
  TurboJpegHandle &operator=(TurboJpegHandle &&)      = delete;

  /* Supplies the decoder handle needed by the TurboJPEG functions below. */
  tjhandle get() const noexcept {
    return m_handle;
  }

private:
  tjhandle m_handle{nullptr};
};

std::filesystem::path validateAndResolveJpegFilePath(const std::filesystem::path &sourceFilePath) {
  if (sourceFilePath.empty()) {
    throw std::invalid_argument("JPEG decoder requires a file path");
  }

  std::error_code filesystemError;
  const auto      absoluteFilePath = std::filesystem::absolute(sourceFilePath, filesystemError);
  if (filesystemError || !std::filesystem::is_regular_file(absoluteFilePath, filesystemError) || filesystemError) {
    throw std::runtime_error("JPEG image file does not exist: " + sourceFilePath.string());
  }
  return absoluteFilePath;
}

std::vector<std::uint8_t> readJpegFile(const std::filesystem::path &jpegFilePath) {
  std::ifstream jpegFile(jpegFilePath, std::ios::binary | std::ios::ate);
  if (!jpegFile) {
    throw std::runtime_error("Could not open JPEG image for decoding: " + jpegFilePath.string());
  }

  const std::streamoff fileSize = jpegFile.tellg();
  if (fileSize <= 0 || static_cast<std::uintmax_t>(fileSize) > maximumCompressedJpegFileSizeInBytes ||
      static_cast<std::uintmax_t>(fileSize) > std::numeric_limits<unsigned long>::max()) {
    throw std::runtime_error("JPEG image is empty or larger than the 10 MiB compressed-file limit: " +
                             jpegFilePath.string());
  }

  std::vector<std::uint8_t> jpegBytes(static_cast<std::size_t>(fileSize));
  jpegFile.seekg(0, std::ios::beg);
  jpegFile.read(reinterpret_cast<char *>(jpegBytes.data()), fileSize);
  if (!jpegFile) {
    throw std::runtime_error("Could not read JPEG image: " + jpegFilePath.string());
  }
  return jpegBytes;
}

bool scalingFactorIsAllowed(const tjscalingfactor &scalingFactor, int originalWidth, int originalHeight,
                            const JpegDecodeRequest &decodeRequest) {
  if (scalingFactor.num <= 0 || scalingFactor.denom <= 0 || scalingFactor.num > scalingFactor.denom) {
    return false;
  }

  // TJSCALED calculates a width or height; it does not resize image pixels.
  // A tjscalingfactor stores a fraction: num / denom. For example, num=1
  // and denom=2 mean half size. The macro rounds up to a whole pixel:
  //
  //   (dimension * num + denom - 1) / denom
  //   101 pixels at 1/2 scale -> 51 pixels, not 50.
  //
  // Use this same calculation when checking limits and allocating pixels.
  const std::uint32_t scaledWidth    = static_cast<std::uint32_t>(TJSCALED(originalWidth, scalingFactor));
  const std::uint32_t scaledHeight   = static_cast<std::uint32_t>(TJSCALED(originalHeight, scalingFactor));
  const bool          widthIsAllowed = decodeRequest.maximumWidth == 0U || scaledWidth <= decodeRequest.maximumWidth;
  const bool heightIsAllowed         = decodeRequest.maximumHeight == 0U || scaledHeight <= decodeRequest.maximumHeight;
  const bool percentageIsAllowed     = static_cast<std::uint32_t>(scalingFactor.num) * 100U <=
                                   static_cast<std::uint32_t>(scalingFactor.denom) * decodeRequest.maximumScalePercent;
  return widthIsAllowed && heightIsAllowed && percentageIsAllowed;
}

tjscalingfactor chooseScalingFactor(int originalWidth, int originalHeight, const JpegDecodeRequest &decodeRequest) {
  // tjGetScalingFactors returns the decoder's supported ratios, such as
  // 1/1, 1/2, and 1/8, and writes the list length into numberOfScalingFactors.
  // We choose from this list because the decoder cannot use any arbitrary
  // percentage requested by Python.
  int                    numberOfScalingFactors = 0;
  const tjscalingfactor *scalingFactors         = tjGetScalingFactors(&numberOfScalingFactors);
  if (scalingFactors == nullptr || numberOfScalingFactors <= 0) {
    throw std::runtime_error("libjpeg-turbo did not provide JPEG scaling factors");
  }

  const tjscalingfactor *selectedScalingFactor = nullptr;
  for (int factorIndex = 0; factorIndex < numberOfScalingFactors; ++factorIndex) {
    const tjscalingfactor &candidate = scalingFactors[factorIndex];
    if (!scalingFactorIsAllowed(candidate, originalWidth, originalHeight, decodeRequest)) {
      continue;
    }
    if (selectedScalingFactor == nullptr ||
        candidate.num * selectedScalingFactor->denom > selectedScalingFactor->num * candidate.denom) {
      selectedScalingFactor = &candidate;
    }
  }

  if (selectedScalingFactor == nullptr) {
    throw std::runtime_error("JPEG image cannot fit within the requested size using a libjpeg-turbo scaling factor");
  }
  return *selectedScalingFactor;
}

class LibjpegTurboJpegImageDecoder final : public IJpegImageDecoder {
public:
  std::shared_ptr<const DecodedJpegImage> decode(const JpegDecodeRequest &decodeRequest) override {
    if (decodeRequest.maximumScalePercent < minimumJpegScalePercent ||
        decodeRequest.maximumScalePercent > maximumJpegScalePercent) {
      throw std::invalid_argument("JPEG decoder scale percentage must be between 13 and 100");
    }

    const std::filesystem::path     jpegFilePath = validateAndResolveJpegFilePath(decodeRequest.sourceFilePath);
    const std::vector<std::uint8_t> jpegBytes    = readJpegFile(jpegFilePath);
    TurboJpegHandle                 decoder;
    int                             originalWidth     = 0;
    int                             originalHeight    = 0;
    int                             chromaSubsampling = 0;
    int                             jpegColorSpace    = 0;
    // tjDecompressHeader3 reads image details into these four variables,
    // without decoding the pixels. This lets us check dimensions before
    // allocating the pixel buffer. A return value of zero means success.
    if (tjDecompressHeader3(decoder.get(), jpegBytes.data(), static_cast<unsigned long>(jpegBytes.size()),
                            &originalWidth, &originalHeight, &chromaSubsampling, &jpegColorSpace) != 0) {
      // tjGetErrorStr2 describes the last error reported by this decoder.
      throw std::runtime_error("libjpeg-turbo could not read '" + decodeRequest.sourceFilePath.string() +
                               "': " + tjGetErrorStr2(decoder.get()));
    }
    // The API also returns how colour samples were stored and which colour
    // space the JPEG uses. Our size checks only need the width and height.
    (void)chromaSubsampling;
    (void)jpegColorSpace;

    if (originalWidth <= 0 || originalHeight <= 0 ||
        static_cast<std::uint32_t>(originalWidth) > maximumJpegDimensionInPixels ||
        static_cast<std::uint32_t>(originalHeight) > maximumJpegDimensionInPixels ||
        static_cast<std::uint64_t>(originalWidth) * static_cast<std::uint64_t>(originalHeight) >
            maximumJpegPixelCount) {
      throw std::runtime_error("JPEG dimensions are larger than the supported safety limit");
    }

    // Calculate the chosen output dimensions with the same rounding used
    // above. The actual decoding and scaling happen in tjDecompress2 below.
    const tjscalingfactor scalingFactor    = chooseScalingFactor(originalWidth, originalHeight, decodeRequest);
    const int             decodedWidth     = TJSCALED(originalWidth, scalingFactor);
    const int             decodedHeight    = TJSCALED(originalHeight, scalingFactor);
    constexpr std::size_t rgbBytesPerPixel = 3U;
    const std::uint64_t   rowStride        = static_cast<std::uint64_t>(decodedWidth) * rgbBytesPerPixel;
    const std::uint64_t   pixelByteCount   = rowStride * static_cast<std::uint64_t>(decodedHeight);
    if (decodedWidth <= 0 || decodedHeight <= 0 || rowStride > std::numeric_limits<std::uint32_t>::max() ||
        pixelByteCount > std::numeric_limits<std::size_t>::max()) {
      throw std::runtime_error("Decoded JPEG dimensions are too large");
    }
    if (pixelByteCount > maximumDecodedJpegPixelBytes) {
      throw std::runtime_error("Decoded JPEG is too large to fit in the image memory limit");
    }

    auto decodedImage              = std::make_shared<DecodedJpegImage>();
    decodedImage->sourceFilePath   = jpegFilePath;
    decodedImage->width            = static_cast<std::uint32_t>(decodedWidth);
    decodedImage->height           = static_cast<std::uint32_t>(decodedHeight);
    decodedImage->rowStrideInBytes = static_cast<std::uint32_t>(rowStride);
    decodedImage->bgrPixelBytes.resize(static_cast<std::size_t>(pixelByteCount));

    // LVGL calls this format RGB888, but its bytes are stored as blue, green,
    // then red. Ask libjpeg-turbo for the same order so red and blue are not
    // swapped on the screen.
    // https://github.com/lvgl/lvgl/blob/v9.5.0/src/misc/lv_color.h
    //
    // tjDecompress2 decodes jpegBytes directly into our pixel vector at the
    // chosen size. rowStride is the number of bytes in one output row.
    // TJPF_BGR requests three bytes per pixel in blue, green, red order.
    // The final 0 selects no extra flags; a non-zero return reports failure.
    if (tjDecompress2(decoder.get(), jpegBytes.data(), static_cast<unsigned long>(jpegBytes.size()),
                      decodedImage->bgrPixelBytes.data(), decodedWidth, static_cast<int>(rowStride), decodedHeight,
                      TJPF_BGR, 0) != 0) {
      // Include the decoder's reason, for example damaged JPEG data.
      throw std::runtime_error("libjpeg-turbo could not decode '" + decodeRequest.sourceFilePath.string() +
                               "': " + tjGetErrorStr2(decoder.get()));
    }
    return decodedImage;
  }
};

} // namespace

std::unique_ptr<IJpegImageDecoder> makeLibjpegTurboJpegImageDecoder() {
  return std::make_unique<LibjpegTurboJpegImageDecoder>();
}

} // namespace internal
} // namespace ui
} // namespace iot
