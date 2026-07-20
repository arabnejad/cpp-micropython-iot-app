#pragma once

#include "ijpeg_image_decoder.h"

#include "iot/logging/logger.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace iot {
namespace ui {
namespace internal {

/*
 * Loads JPEG pixels on the main thread and reuses recently decoded images.
 * The cache only evicts entries that no widget or queued command still uses.
 * Replacing an image can therefore fail if the old and new pixels do not fit
 * together. A decode also needs temporary memory before cache admission.
 */
class JpegImageLoader {
public:
  JpegImageLoader(std::unique_ptr<IJpegImageDecoder> jpegImageDecoder, std::size_t maximumRetainedPixelBytes);

  JpegImageLoader(const JpegImageLoader &)            = delete;
  JpegImageLoader &operator=(const JpegImageLoader &) = delete;
  JpegImageLoader(JpegImageLoader &&)                 = delete;
  JpegImageLoader &operator=(JpegImageLoader &&)      = delete;

  /* Returns cached pixels or loads the JPEG on the calling thread. */
  std::shared_ptr<const DecodedJpegImage> loadImage(const JpegDecodeRequest &decodeRequest);

  /* Releases pixels retained for reuse. An active LVGL widget keeps its own reference. */
  void clearCache() noexcept;

private:
  struct CacheEntry {
    std::shared_ptr<const DecodedJpegImage> decodedImage;
    std::uint64_t                           lastUseNumber{0};
  };

  static std::string createCacheKey(const JpegDecodeRequest &decodeRequest);
  void storeDecodedImage(const std::string &cacheKey, std::shared_ptr<const DecodedJpegImage> decodedImage);
  bool removeLeastRecentlyUsedImageNotInUse();

  logging::Logger                             m_logger{"JpegImageLoader"};
  std::unique_ptr<IJpegImageDecoder>          m_jpegImageDecoder;
  const std::size_t                           m_maximumRetainedPixelBytes;
  std::size_t                                 m_retainedPixelBytes{0};
  std::uint64_t                               m_nextUseNumber{1};
  std::unordered_map<std::string, CacheEntry> m_cachedImages;
};

} // namespace internal
} // namespace ui
} // namespace iot
