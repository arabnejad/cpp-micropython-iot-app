#include "ui/internal/jpeg_image_loader.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <thread>

namespace iot {
namespace ui {
namespace internal {
namespace {

class RecordingJpegImageDecoder final : public IJpegImageDecoder {
public:
  std::shared_ptr<const DecodedJpegImage> decode(const JpegDecodeRequest &decodeRequest) override {
    ++numberOfDecodeCalls;
    decodingThreadId = std::this_thread::get_id();
    if (shouldFail) {
      throw std::runtime_error("JPEG decoding failed");
    }

    auto decodedImage              = std::make_shared<DecodedJpegImage>();
    decodedImage->sourceFilePath   = decodeRequest.sourceFilePath;
    decodedImage->width            = 4U;
    decodedImage->height           = 5U;
    decodedImage->rowStrideInBytes = 12U;
    decodedImage->bgrPixelBytes.assign(numberOfPixelBytesToReturn, 42U);
    return decodedImage;
  }

  std::size_t     numberOfPixelBytesToReturn{60U};
  std::size_t     numberOfDecodeCalls{0U};
  std::thread::id decodingThreadId;
  bool            shouldFail{false};
};

TEST(JpegImageLoaderTest, LoadsOnTheCallingThreadAndReusesPixelsFromTheMemoryCache) {
  auto                    decoder     = std::make_unique<RecordingJpegImageDecoder>();
  auto                   *decoderView = decoder.get();
  JpegImageLoader         imageLoader(std::move(decoder), 1024U);
  const auto              callerThreadId = std::this_thread::get_id();
  const JpegDecodeRequest decodeRequest{"first.jpg", 100U, 100U, 100U};

  const auto firstResult  = imageLoader.loadImage(decodeRequest);
  const auto secondResult = imageLoader.loadImage(decodeRequest);

  EXPECT_EQ(firstResult, secondResult);
  EXPECT_EQ(decoderView->numberOfDecodeCalls, 1U);
  EXPECT_EQ(decoderView->decodingThreadId, callerThreadId);
}

TEST(JpegImageLoaderTest, ClearsTheApplicationCacheAndDecodesTheFileAgain) {
  auto                    decoder     = std::make_unique<RecordingJpegImageDecoder>();
  auto                   *decoderView = decoder.get();
  JpegImageLoader         imageLoader(std::move(decoder), 1024U);
  const JpegDecodeRequest decodeRequest{"first.jpg", 100U, 100U, 100U};

  imageLoader.loadImage(decodeRequest);
  imageLoader.clearCache();
  imageLoader.loadImage(decodeRequest);

  EXPECT_EQ(decoderView->numberOfDecodeCalls, 2U);
}

TEST(JpegImageLoaderTest, RemovesTheOldestImageWhenTheCacheIsFull) {
  auto            decoder     = std::make_unique<RecordingJpegImageDecoder>();
  auto           *decoderView = decoder.get();
  JpegImageLoader imageLoader(std::move(decoder), 100U);

  imageLoader.loadImage({"first.jpg", 0U, 0U, 100U});
  imageLoader.loadImage({"second.jpg", 0U, 0U, 100U});
  imageLoader.loadImage({"first.jpg", 0U, 0U, 100U});

  EXPECT_EQ(decoderView->numberOfDecodeCalls, 3U);
}

TEST(JpegImageLoaderTest, DoesNotEvictPixelsStillUsedByAnImageWidget) {
  auto            decoder = std::make_unique<RecordingJpegImageDecoder>();
  JpegImageLoader imageLoader(std::move(decoder), 100U);
  auto            imageStillInUse = imageLoader.loadImage({"first.jpg", 0U, 0U, 100U});

  EXPECT_THROW(imageLoader.loadImage({"second.jpg", 0U, 0U, 100U}), std::runtime_error);

  imageStillInUse.reset();
  EXPECT_NO_THROW(imageLoader.loadImage({"second.jpg", 0U, 0U, 100U}));
}

TEST(JpegImageLoaderTest, DecodesAFileAgainAfterItsContentsChange) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                jpegPath = temporaryDirectory.path() / "changing.jpg";
  std::ofstream(jpegPath) << "first";

  auto            decoder     = std::make_unique<RecordingJpegImageDecoder>();
  auto           *decoderView = decoder.get();
  JpegImageLoader imageLoader(std::move(decoder), 1024U);
  imageLoader.loadImage({jpegPath, 0U, 0U, 100U});

  std::ofstream(jpegPath, std::ios::trunc) << "different contents";
  imageLoader.loadImage({jpegPath, 0U, 0U, 100U});

  EXPECT_EQ(decoderView->numberOfDecodeCalls, 2U);
}

TEST(JpegImageLoaderTest, RejectsPixelsLargerThanItsMemoryLimit) {
  auto            decoder = std::make_unique<RecordingJpegImageDecoder>();
  JpegImageLoader imageLoader(std::move(decoder), 50U);

  EXPECT_THROW(imageLoader.loadImage({"large.jpg", 0U, 0U, 100U}), std::runtime_error);
}

TEST(JpegImageLoaderTest, ReturnsDecodeErrorsToTheCallingThread) {
  auto decoder        = std::make_unique<RecordingJpegImageDecoder>();
  decoder->shouldFail = true;
  JpegImageLoader imageLoader(std::move(decoder), 1024U);

  EXPECT_THROW(imageLoader.loadImage({"bad.jpg", 0U, 0U, 100U}), std::runtime_error);
}

TEST(JpegImageLoaderTest, RequiresADecoderAndANonEmptyCache) {
  EXPECT_THROW(JpegImageLoader(nullptr, 1024U), std::invalid_argument);
  EXPECT_THROW(JpegImageLoader(std::make_unique<RecordingJpegImageDecoder>(), 0U), std::invalid_argument);
}

TEST(JpegImageLoaderTest, KeepsTheOriginalCachedImageWhenAReplacementCannotFitAlongsideIt) {
  auto                    decoder     = std::make_unique<RecordingJpegImageDecoder>();
  auto                   *decoderView = decoder.get();
  JpegImageLoader         imageLoader(std::move(decoder), 100U);
  const JpegDecodeRequest originalRequest{"original.jpg", 0U, 0U, 100U};
  const auto              visibleImage = imageLoader.loadImage(originalRequest);

  EXPECT_THROW(imageLoader.loadImage({"replacement.jpg", 0U, 0U, 100U}), std::runtime_error);
  EXPECT_EQ(imageLoader.loadImage(originalRequest), visibleImage);
  EXPECT_EQ(decoderView->numberOfDecodeCalls, 2U);
  EXPECT_EQ(visibleImage->bgrPixelBytes.size(), 60U);
}

TEST(JpegImageLoaderTest, AllowsOldAndNewPixelsToCoexistWhenBothFitWithinTheLimit) {
  JpegImageLoader imageLoader(std::make_unique<RecordingJpegImageDecoder>(), 120U);
  const auto      oldImage = imageLoader.loadImage({"old.jpg", 0U, 0U, 100U});
  const auto      newImage = imageLoader.loadImage({"new.jpg", 0U, 0U, 100U});

  EXPECT_NE(oldImage, newImage);
  EXPECT_EQ(oldImage->bgrPixelBytes.size() + newImage->bgrPixelBytes.size(), 120U);
}

TEST(JpegImageLoaderTest, ClearingTheCacheKeepsPixelsAliveUntilTheRenderOwnerReleasesThem) {
  JpegImageLoader imageLoader(std::make_unique<RecordingJpegImageDecoder>(), 100U);
  auto            pixelsOwnedByRenderCommand           = imageLoader.loadImage({"visible.jpg", 0U, 0U, 100U});
  std::weak_ptr<const DecodedJpegImage> previousPixels = pixelsOwnedByRenderCommand;

  imageLoader.clearCache();
  EXPECT_FALSE(previousPixels.expired());
  EXPECT_EQ(pixelsOwnedByRenderCommand->bgrPixelBytes.size(), 60U);

  pixelsOwnedByRenderCommand.reset();
  EXPECT_TRUE(previousPixels.expired());
}

TEST(JpegImageLoaderTest, RejectsAnEmptyDecodeWithoutAddingItToTheCache) {
  auto  decoder                       = std::make_unique<RecordingJpegImageDecoder>();
  auto *decoderView                   = decoder.get();
  decoder->numberOfPixelBytesToReturn = 0U;
  JpegImageLoader imageLoader(std::move(decoder), 100U);
  EXPECT_THROW(imageLoader.loadImage({"empty.jpg", 0U, 0U, 100U}), std::runtime_error);

  decoderView->numberOfPixelBytesToReturn = 60U;
  EXPECT_NO_THROW(imageLoader.loadImage({"empty.jpg", 0U, 0U, 100U}));
  EXPECT_EQ(decoderView->numberOfDecodeCalls, 2U);
}

} // namespace
} // namespace internal
} // namespace ui
} // namespace iot
