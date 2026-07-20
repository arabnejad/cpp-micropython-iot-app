#include "internal/jpeg_image_loader.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace iot {
namespace ui {
namespace internal {

JpegImageLoader::JpegImageLoader(std::unique_ptr<IJpegImageDecoder> jpegImageDecoder,
                                 std::size_t                        maximumRetainedPixelBytes)
    : m_jpegImageDecoder(std::move(jpegImageDecoder)), m_maximumRetainedPixelBytes(maximumRetainedPixelBytes) {
  if (!m_jpegImageDecoder) {
    throw std::invalid_argument("JpegImageLoader requires a JPEG decoder");
  }
  if (m_maximumRetainedPixelBytes == 0U) {
    throw std::invalid_argument("JpegImageLoader requires a non-zero cache size");
  }
}

std::shared_ptr<const DecodedJpegImage> JpegImageLoader::loadImage(const JpegDecodeRequest &decodeRequest) {
  const std::string cacheKey    = createCacheKey(decodeRequest);
  const auto        cachedImage = m_cachedImages.find(cacheKey);
  if (cachedImage != m_cachedImages.end()) {
    cachedImage->second.lastUseNumber = m_nextUseNumber++;
    IOT_LOG_DEBUG(m_logger, "Using decoded JPEG from memory; file=", decodeRequest.sourceFilePath,
                  ", width=", cachedImage->second.decodedImage->width,
                  ", height=", cachedImage->second.decodedImage->height);
    return cachedImage->second.decodedImage;
  }

  IOT_LOG_DEBUG(m_logger, "Decoding JPEG; file=", decodeRequest.sourceFilePath,
                ", maximumWidth=", decodeRequest.maximumWidth, ", maximumHeight=", decodeRequest.maximumHeight,
                ", maximumScalePercent=", decodeRequest.maximumScalePercent);
  auto decodedImage = m_jpegImageDecoder->decode(decodeRequest);
  if (!decodedImage || decodedImage->bgrPixelBytes.empty()) {
    throw std::runtime_error("JPEG decoder returned no pixel data");
  }
  storeDecodedImage(cacheKey, decodedImage);
  return decodedImage;
}

void JpegImageLoader::clearCache() noexcept {
  m_cachedImages.clear();
  m_retainedPixelBytes = 0U;
  IOT_LOG_DEBUG(m_logger, "Released the decoded JPEG cache");
}

std::string JpegImageLoader::createCacheKey(const JpegDecodeRequest &decodeRequest) {
  std::error_code filesystemError;
  const auto      absoluteFilePath = std::filesystem::absolute(decodeRequest.sourceFilePath, filesystemError);
  const auto     &filePathForKey   = filesystemError ? decodeRequest.sourceFilePath : absoluteFilePath;

  filesystemError.clear();
  const std::uintmax_t fileSizeInBytes = std::filesystem::file_size(filePathForKey, filesystemError);
  const std::string    fileSizeText    = filesystemError ? "unavailable" : std::to_string(fileSizeInBytes);

  filesystemError.clear();
  const auto        lastWriteTime = std::filesystem::last_write_time(filePathForKey, filesystemError);
  const std::string lastWriteTimeText =
      filesystemError ? "unavailable" : std::to_string(lastWriteTime.time_since_epoch().count());

  return filePathForKey.string() + '|' + fileSizeText + '|' + lastWriteTimeText + '|' +
         std::to_string(decodeRequest.maximumWidth) + '|' + std::to_string(decodeRequest.maximumHeight) + '|' +
         std::to_string(decodeRequest.maximumScalePercent);
}

void JpegImageLoader::storeDecodedImage(const std::string                      &cacheKey,
                                        std::shared_ptr<const DecodedJpegImage> decodedImage) {
  const std::size_t decodedPixelBytes = decodedImage->bgrPixelBytes.size();
  if (decodedPixelBytes > m_maximumRetainedPixelBytes) {
    throw std::runtime_error("Decoded JPEG needs " + std::to_string(decodedPixelBytes) +
                             " bytes, which is more than the image-memory limit of " +
                             std::to_string(m_maximumRetainedPixelBytes) + " bytes");
  }

  while (m_retainedPixelBytes > m_maximumRetainedPixelBytes - decodedPixelBytes) {
    if (!removeLeastRecentlyUsedImageNotInUse()) {
      throw std::runtime_error("There is not enough image memory. Delete an image or use a smaller scale.");
    }
  }
  m_cachedImages.emplace(cacheKey, CacheEntry{std::move(decodedImage), m_nextUseNumber++});
  m_retainedPixelBytes += decodedPixelBytes;
}

bool JpegImageLoader::removeLeastRecentlyUsedImageNotInUse() {
  auto imageToRemove = m_cachedImages.end();
  for (auto cacheEntry = m_cachedImages.begin(); cacheEntry != m_cachedImages.end(); ++cacheEntry) {
    // A count above one means a queued render command or an LVGL widget still
    // owns these pixels. Removing the cache entry would not release that
    // memory, so look for an image that only the cache owns.
    if (cacheEntry->second.decodedImage.use_count() != 1) {
      continue;
    }
    if (imageToRemove == m_cachedImages.end() ||
        cacheEntry->second.lastUseNumber < imageToRemove->second.lastUseNumber) {
      imageToRemove = cacheEntry;
    }
  }
  if (imageToRemove == m_cachedImages.end()) {
    return false;
  }

  m_retainedPixelBytes -= imageToRemove->second.decodedImage->bgrPixelBytes.size();
  m_cachedImages.erase(imageToRemove);
  return true;
}

} // namespace internal
} // namespace ui
} // namespace iot
