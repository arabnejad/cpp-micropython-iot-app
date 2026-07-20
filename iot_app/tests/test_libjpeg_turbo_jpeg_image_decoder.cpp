#include "ui/internal/ijpeg_image_decoder.h"

#include "test_jpeg_file.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace iot {
namespace ui {
namespace internal {
namespace {

void replaceJpegDimensions(const std::filesystem::path &jpegFilePath, std::uint16_t width, std::uint16_t height) {
  std::ifstream              inputFile(jpegFilePath, std::ios::binary);
  std::vector<unsigned char> jpegBytes{std::istreambuf_iterator<char>(inputFile), std::istreambuf_iterator<char>()};
  for (std::size_t byteIndex = 0U; byteIndex + 8U < jpegBytes.size(); ++byteIndex) {
    if (jpegBytes[byteIndex] != 0xffU || jpegBytes[byteIndex + 1U] != 0xc0U) {
      continue;
    }
    jpegBytes[byteIndex + 5U] = static_cast<unsigned char>(height >> 8U);
    jpegBytes[byteIndex + 6U] = static_cast<unsigned char>(height & 0xffU);
    jpegBytes[byteIndex + 7U] = static_cast<unsigned char>(width >> 8U);
    jpegBytes[byteIndex + 8U] = static_cast<unsigned char>(width & 0xffU);
    std::ofstream outputFile(jpegFilePath, std::ios::binary | std::ios::trunc);
    outputFile.write(reinterpret_cast<const char *>(jpegBytes.data()), static_cast<std::streamsize>(jpegBytes.size()));
    return;
  }
  throw std::runtime_error("Test JPEG does not contain a baseline frame header");
}

TEST(LibjpegTurboJpegImageDecoderTest, DecodesJpegPixelsAtTheirOriginalSize) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                jpegFilePath = temporaryDirectory.path() / "image.jpg";
  tests::writeTestJpegFile(jpegFilePath);
  auto jpegImageDecoder = makeLibjpegTurboJpegImageDecoder();

  const auto decodedImage = jpegImageDecoder->decode({jpegFilePath, 0U, 0U, 100U});

  EXPECT_EQ(decodedImage->sourceFilePath, jpegFilePath);
  EXPECT_EQ(decodedImage->width, 16U);
  EXPECT_EQ(decodedImage->height, 12U);
  EXPECT_EQ(decodedImage->rowStrideInBytes, 48U);
  ASSERT_EQ(decodedImage->bgrPixelBytes.size(), 576U);
  // The test JPEG is one solid RGB colour: (51, 103, 153). LVGL stores its
  // RGB888 pixels as B, G, R bytes, so this checks the channel order too.
  EXPECT_EQ(decodedImage->bgrPixelBytes[0], 153U);
  EXPECT_EQ(decodedImage->bgrPixelBytes[1], 103U);
  EXPECT_EQ(decodedImage->bgrPixelBytes[2], 51U);
}

TEST(LibjpegTurboJpegImageDecoderTest, ChoosesTheLargestSupportedSizeWithinTheRequestedLimits) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                jpegFilePath = temporaryDirectory.path() / "image.jpg";
  tests::writeTestJpegFile(jpegFilePath);
  auto jpegImageDecoder = makeLibjpegTurboJpegImageDecoder();

  const auto halfSizeImage           = jpegImageDecoder->decode({jpegFilePath, 8U, 6U, 100U});
  const auto belowNinetyPercentImage = jpegImageDecoder->decode({jpegFilePath, 0U, 0U, 90U});

  EXPECT_EQ(halfSizeImage->width, 8U);
  EXPECT_EQ(halfSizeImage->height, 6U);
  EXPECT_LE(belowNinetyPercentImage->width, 15U);
  EXPECT_LE(belowNinetyPercentImage->height, 11U);
  EXPECT_GT(belowNinetyPercentImage->width, 8U);
  EXPECT_GT(belowNinetyPercentImage->height, 6U);
}

TEST(LibjpegTurboJpegImageDecoderTest, ReportsInvalidRequestsAndUnreadableJpegFiles) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                invalidJpegPath = temporaryDirectory.path() / "invalid.jpg";
  std::ofstream(invalidJpegPath) << "not a jpeg";
  auto jpegImageDecoder = makeLibjpegTurboJpegImageDecoder();

  EXPECT_THROW(jpegImageDecoder->decode({{}, 0U, 0U, 100U}), std::invalid_argument);
  EXPECT_THROW(jpegImageDecoder->decode({invalidJpegPath, 0U, 0U, 12U}), std::invalid_argument);
  EXPECT_THROW(jpegImageDecoder->decode({temporaryDirectory.path() / "missing.jpg", 0U, 0U, 100U}), std::runtime_error);
  EXPECT_THROW(jpegImageDecoder->decode({invalidJpegPath, 0U, 0U, 100U}), std::runtime_error);
}

TEST(LibjpegTurboJpegImageDecoderTest, RecognizesJpegContentWithoutDependingOnTheFilenameExtension) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                uppercaseJpegPath = temporaryDirectory.path() / "image.JPEG";
  const auto                unusualFilePath   = temporaryDirectory.path() / "image.bin";
  tests::writeTestJpegFile(uppercaseJpegPath);
  tests::writeTestJpegFile(unusualFilePath);
  auto jpegImageDecoder = makeLibjpegTurboJpegImageDecoder();

  EXPECT_NO_THROW(jpegImageDecoder->decode({uppercaseJpegPath, 0U, 0U, 100U}));
  EXPECT_NO_THROW(jpegImageDecoder->decode({unusualFilePath, 0U, 0U, 100U}));
}

TEST(LibjpegTurboJpegImageDecoderTest, RejectsEmptyOversizedAndUnscalableJpegFiles) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                emptyJpegPath = temporaryDirectory.path() / "empty.jpg";
  std::ofstream             emptyJpegFile(emptyJpegPath);
  emptyJpegFile.close();
  const auto oversizedJpegPath = temporaryDirectory.path() / "oversized.jpg";
  {
    std::ofstream oversizedJpegFile(oversizedJpegPath, std::ios::binary | std::ios::trunc);
    oversizedJpegFile.seekp((10 * 1024 * 1024));
    oversizedJpegFile.put('x');
  }
  const auto smallJpegPath = temporaryDirectory.path() / "small.jpg";
  tests::writeTestJpegFile(smallJpegPath);
  auto jpegImageDecoder = makeLibjpegTurboJpegImageDecoder();

  EXPECT_THROW(jpegImageDecoder->decode({emptyJpegPath, 0U, 0U, 100U}), std::runtime_error);
  EXPECT_THROW(jpegImageDecoder->decode({oversizedJpegPath, 0U, 0U, 100U}), std::runtime_error);
  EXPECT_THROW(jpegImageDecoder->decode({smallJpegPath, 1U, 1U, 13U}), std::runtime_error);
}

TEST(LibjpegTurboJpegImageDecoderTest, RejectsUnsafeOriginalAndDecodedImageSizes) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                excessiveDimensionPath = temporaryDirectory.path() / "too-wide.jpg";
  tests::writeTestJpegFile(excessiveDimensionPath);
  replaceJpegDimensions(excessiveDimensionPath, 9000U, 1U);

  const auto excessiveDecodedSizePath = temporaryDirectory.path() / "too-many-decoded-bytes.jpg";
  tests::writeTestJpegFile(excessiveDecodedSizePath);
  replaceJpegDimensions(excessiveDecodedSizePath, 4096U, 4096U);
  auto jpegImageDecoder = makeLibjpegTurboJpegImageDecoder();

  EXPECT_THROW(jpegImageDecoder->decode({excessiveDimensionPath, 0U, 0U, 100U}), std::runtime_error);
  EXPECT_THROW(jpegImageDecoder->decode({excessiveDecodedSizePath, 0U, 0U, 100U}), std::runtime_error);
}

} // namespace
} // namespace internal
} // namespace ui
} // namespace iot
