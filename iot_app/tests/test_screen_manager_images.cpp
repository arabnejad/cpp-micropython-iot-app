#include "iot/ui/screen_manager.h"

#include "test_jpeg_file.h"
#include "test_support.h"

#include <gtest/gtest.h>

namespace iot {
namespace ui {
namespace {

TEST(ScreenManagerImageTest, RejectsWidgetIdsAndReleasesCachedImagesWhenTheRenderThreadStops) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                jpegPath = temporaryDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(jpegPath);
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 8U);
  screenManager.start();
  const auto previousTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Old renderer"});
  const auto previousImageId   = screenManager.drawJpegImage({jpegPath, 10, 20, 100U});
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->jpegImagesById.count(previousImageId) == 1U;
  }));
  std::weak_ptr<const DecodedJpegImage> previousImagePixels;
  {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    previousImagePixels = recordingRenderBackendView->jpegImagesById.at(previousImageId).decodedImage;
  }

  screenManager.stop();
  EXPECT_TRUE(previousImagePixels.expired());
  screenManager.start();

  EXPECT_THROW(screenManager.updateTextBox(previousTextBoxId, "Stale"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveTextBox(previousTextBoxId, 10, 20), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteTextBox(previousTextBoxId), std::invalid_argument);
  EXPECT_THROW(screenManager.moveJpegImage(previousImageId, 10, 20), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteJpegImage(previousImageId), std::invalid_argument);
  const auto newTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "New renderer"});
  EXPECT_NE(newTextBoxId, previousTextBoxId);
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
}

TEST(ScreenManagerImageTest, ClearReleasesWidgetIdsAndDecodedImagesFromThePreviousScreen) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                jpegPath = temporaryDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(jpegPath);
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 8U);
  screenManager.start();

  const auto textBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Previous screen"});
  const auto imageId   = screenManager.drawJpegImage({jpegPath, 10, 20, 100U});
  screenManager.setBackgroundJpegImage({jpegPath, BackgroundImageMode::Center, 100U});
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->textBoxesById.count(textBoxId) == 1U &&
           recordingRenderBackendView->jpegImagesById.count(imageId) == 1U &&
           recordingRenderBackendView->backgroundJpegImage.has_value();
  }));
  std::weak_ptr<const DecodedJpegImage> decodedImagePixels;
  {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    decodedImagePixels = recordingRenderBackendView->jpegImagesById.at(imageId).decodedImage;
  }

  screenManager.clear({8, 13, 22});

  EXPECT_THROW(screenManager.updateTextBox(textBoxId, "Stale"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveJpegImage(imageId, 20, 30), std::invalid_argument);
  EXPECT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->textBoxesById.empty() && recordingRenderBackendView->jpegImagesById.empty() &&
           !recordingRenderBackendView->backgroundJpegImage.has_value() && decodedImagePixels.expired();
  }));
  screenManager.stop();
}

TEST(ScreenManagerImageTest, SendsNormalAndBackgroundJpegCommandsToTheRenderThread) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                firstJpegPath  = temporaryDirectory.path() / "first.jpg";
  const auto                secondJpegPath = temporaryDirectory.path() / "second.jpeg";
  tests::writeTestJpegFile(firstJpegPath);
  tests::writeTestJpegFile(secondJpegPath);

  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U);
  screenManager.start();

  const WidgetId imageId = screenManager.drawJpegImage({firstJpegPath, 10, 20, 75U});
  EXPECT_THROW(screenManager.updateTextBox(imageId, "An image is not a text box"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveTextBox(imageId, 0, 0), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteTextBox(imageId), std::invalid_argument);
  screenManager.replaceJpegImage(imageId, secondJpegPath);
  screenManager.moveJpegImage(imageId, 30, 40);
  screenManager.setJpegImageScale(imageId, 50U);
  screenManager.setBackgroundJpegImage({firstJpegPath, BackgroundImageMode::Fit, 80U});

  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    const auto                  image = recordingRenderBackendView->jpegImagesById.find(imageId);
    return image != recordingRenderBackendView->jpegImagesById.end() && image->second.x == 30 &&
           image->second.y == 40 && image->second.decodedImage->width == 8U &&
           image->second.decodedImage->height == 6U &&
           image->second.decodedImage->sourceFilePath == std::filesystem::absolute(secondJpegPath) &&
           recordingRenderBackendView->backgroundJpegImage.has_value();
  }));

  screenManager.clearBackgroundJpegImage();
  screenManager.deleteJpegImage(imageId);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->jpegImagesById.empty() &&
           !recordingRenderBackendView->backgroundJpegImage.has_value();
  }));
  screenManager.stop();
}

TEST(ScreenManagerImageTest, RejectsInvalidImageRequestsBeforeTheyReachTheRenderThread) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 4U);
  screenManager.start();

  EXPECT_THROW(screenManager.drawJpegImage({"missing.jpg", 0, 0, 100U}), std::runtime_error);
  EXPECT_THROW(screenManager.drawJpegImage({"missing.jpg", 0, 0, 0U}), std::invalid_argument);
  EXPECT_THROW(screenManager.replaceJpegImage(0U, "missing.jpg"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveJpegImage(0U, 0, 0), std::invalid_argument);
  EXPECT_THROW(screenManager.setJpegImageScale(0U, 100U), std::invalid_argument);
  EXPECT_THROW(screenManager.setJpegImageScale(1U, 12U), std::invalid_argument);
  EXPECT_THROW(screenManager.setJpegImageScale(1U, 101U), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteJpegImage(0U), std::invalid_argument);
  screenManager.stop();
}

TEST(ScreenManagerImageTest, FailedImageReplacementKeepsTheExistingWidgetAndBackground) {
  tests::TemporaryDirectory imageDirectory;
  const auto                jpegPath = imageDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(jpegPath);
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 8U);
  screenManager.start();
  const auto textBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Previous screen"});
  const auto imageId   = screenManager.drawJpegImage({jpegPath, 10, 20, 100U});
  screenManager.setBackgroundJpegImage({jpegPath, BackgroundImageMode::Center, 100U});
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->jpegImagesById.count(imageId) == 1U &&
           recordingRenderBackendView->backgroundJpegImage.has_value();
  }));
  std::weak_ptr<const DecodedJpegImage> originalWidgetPixels;
  std::weak_ptr<const DecodedJpegImage> originalBackgroundPixels;
  {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    originalWidgetPixels     = recordingRenderBackendView->jpegImagesById.at(imageId).decodedImage;
    originalBackgroundPixels = recordingRenderBackendView->backgroundJpegImage->decodedImage;
  }

  EXPECT_THROW(screenManager.replaceJpegImage(imageId, imageDirectory.path() / "missing.jpg"), std::runtime_error);
  EXPECT_THROW(
      screenManager.setBackgroundJpegImage({imageDirectory.path() / "missing.jpg", BackgroundImageMode::Center, 100U}),
      std::runtime_error);
  {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    EXPECT_EQ(recordingRenderBackendView->jpegImagesById.at(imageId).decodedImage, originalWidgetPixels.lock());
    EXPECT_EQ(recordingRenderBackendView->backgroundJpegImage->decodedImage, originalBackgroundPixels.lock());
  }
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());

  screenManager.showErrorScreen({{0, 0, 300, 200}, "Application failed"});
  EXPECT_THROW(screenManager.updateTextBox(textBoxId, "Stale"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveJpegImage(imageId, 20, 30), std::invalid_argument);
  EXPECT_TRUE(tests::waitUntil([&] { return originalWidgetPixels.expired() && originalBackgroundPixels.expired(); }));
  screenManager.stop();
}

TEST(ScreenManagerImageTest, RejectedImageDeletionKeepsTheImageId) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                jpegPath = temporaryDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(jpegPath);
  auto          pausedRenderBackend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *pausedRenderBackendView = pausedRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(pausedRenderBackend), 1U);
  screenManager.start();
  const bool rendererPaused = pausedRenderBackendView->waitUntilRenderThreadIsPaused();

  const auto imageId = screenManager.drawJpegImage({jpegPath, 10, 20, 100U});
  EXPECT_THROW(screenManager.deleteJpegImage(imageId), std::runtime_error);

  pausedRenderBackendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(pausedRenderBackendView->renderStateMutex);
    return pausedRenderBackendView->jpegImagesById.count(imageId) == 1U;
  }));
  EXPECT_NO_THROW(screenManager.moveJpegImage(imageId, 30, 40));
  EXPECT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(pausedRenderBackendView->renderStateMutex);
    return pausedRenderBackendView->jpegImagesById.at(imageId).x == 30;
  }));
  screenManager.stop();
}

TEST(ScreenManagerImageTest, RejectsImageCommandsWhenTheRenderQueueIsFull) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                firstJpegPath  = temporaryDirectory.path() / "first.jpg";
  const auto                secondJpegPath = temporaryDirectory.path() / "second.jpg";
  tests::writeTestJpegFile(firstJpegPath);
  tests::writeTestJpegFile(secondJpegPath);

  auto          pausedRenderBackend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *pausedRenderBackendView = pausedRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(pausedRenderBackend), 1U);
  screenManager.start();
  ASSERT_TRUE(pausedRenderBackendView->waitUntilRenderThreadIsPaused());

  const WidgetId firstImageId = screenManager.drawJpegImage({firstJpegPath, 10, 20, 100U});
  EXPECT_THROW(screenManager.drawJpegImage({secondJpegPath, 0, 0, 100U}), std::runtime_error);
  EXPECT_THROW(screenManager.replaceJpegImage(firstImageId, secondJpegPath), std::runtime_error);
  EXPECT_THROW(screenManager.moveJpegImage(firstImageId, 30, 40), std::runtime_error);
  EXPECT_THROW(screenManager.setJpegImageScale(firstImageId, 50U), std::runtime_error);

  // An emergency screen must replace stale work even when the normal queue is full.
  EXPECT_NO_THROW(screenManager.showErrorScreen({{0, 0, 300, 200}, "Application failed"}));

  pausedRenderBackendView->letRenderThreadContinue();
  EXPECT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(pausedRenderBackendView->renderStateMutex);
    return pausedRenderBackendView->lastErrorScreenText == "Application failed";
  }));
  screenManager.stop();
}

TEST(ScreenManagerImageTest, ProcessesAnImageDeletionQueuedBeforeTheImageHasBeenDrawn) {
  tests::TemporaryDirectory imageDirectory;
  const auto                jpegPath = imageDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(jpegPath);
  auto          pausedRenderBackend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *pausedRenderBackendView = pausedRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(pausedRenderBackend), 8U);
  screenManager.start();
  const bool rendererPaused     = pausedRenderBackendView->waitUntilRenderThreadIsPaused();
  WidgetId   deletedImageId     = 0U;
  WidgetId   replacementImageId = 0U;

  EXPECT_NO_THROW(deletedImageId = screenManager.drawJpegImage({jpegPath, 10, 20, 100U}));
  EXPECT_NO_THROW(screenManager.deleteJpegImage(deletedImageId));
  EXPECT_THROW(screenManager.moveJpegImage(deletedImageId, 20, 30), std::invalid_argument);
  EXPECT_NO_THROW(replacementImageId = screenManager.drawJpegImage({jpegPath, 30, 40, 100U}));

  // Resume before any assertion that could end the test, so stop() can join.
  pausedRenderBackendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(pausedRenderBackendView->renderStateMutex);
    return pausedRenderBackendView->jpegImagesById.count(deletedImageId) == 0U &&
           pausedRenderBackendView->jpegImagesById.count(replacementImageId) == 1U;
  }));
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
  screenManager.stop();
}

} // namespace
} // namespace ui
} // namespace iot
