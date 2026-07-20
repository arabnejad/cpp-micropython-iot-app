#include "iot/ui/screen_manager.h"

#include "test_support.h"
#include "test_jpeg_file.h"

#include <gtest/gtest.h>

namespace iot {
namespace ui {
namespace {

bool waitForTextBox(tests::RecordingRenderBackend &recordingRenderBackend, WidgetId textBoxId) {
  return tests::waitUntil([&recordingRenderBackend, textBoxId] {
    std::lock_guard<std::mutex> lock(recordingRenderBackend.renderStateMutex);
    return recordingRenderBackend.textBoxesById.find(textBoxId) != recordingRenderBackend.textBoxesById.end();
  });
}

class FailingRenderBackend final : public IRenderBackend {
public:
  explicit FailingRenderBackend(bool shouldFailDuringInitialization)
      : m_shouldFailDuringInitialization(shouldFailDuringInitialization) {}

  void initialize(const display::ActiveDisplay &) override {
    if (m_shouldFailDuringInitialization) {
      throw std::runtime_error("renderer initialization failed");
    }
  }
  void shutdown() noexcept override {}
  void createTextBox(WidgetId, const TextBoxSpec &) override {
    throw std::runtime_error("renderer drawing failed");
  }
  void          updateTextBox(WidgetId, const std::string &) override {}
  void          moveTextBox(WidgetId, std::int32_t, std::int32_t) override {}
  void          deleteTextBox(WidgetId) override {}
  void          createJpegImage(WidgetId, const DecodedJpegImageSpec &) override {}
  void          replaceJpegImage(WidgetId, std::shared_ptr<const DecodedJpegImage>) override {}
  void          moveJpegImage(WidgetId, std::int32_t, std::int32_t) override {}
  void          deleteJpegImage(WidgetId) override {}
  void          setBackgroundJpegImage(const DecodedBackgroundJpegImageSpec &) override {}
  void          clearBackgroundJpegImage() override {}
  void          fillArea(const FilledAreaSpec &) override {}
  void          showErrorScreen(const TextBoxSpec &) override {}
  void          clear(Color) override {}
  std::uint32_t processEventsAndGetWaitMilliseconds() override {
    return 500U;
  }

private:
  bool m_shouldFailDuringInitialization{false};
};

TEST(ScreenManagerTest, SendsTextBoxCreationAndUpdatesToTheRenderThread) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U);
  screenManager.start();

  TextBoxSpec textBoxSpecification;
  textBoxSpecification.text   = "First text";
  textBoxSpecification.bounds = {10, 20, 300, 80};
  const WidgetId textBoxId    = screenManager.drawTextBox(textBoxSpecification);
  ASSERT_TRUE(waitForTextBox(*recordingRenderBackendView, textBoxId));

  screenManager.updateTextBox(textBoxId, "Updated text");
  screenManager.moveTextBox(textBoxId, 40, 50);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    const auto                  textBox = recordingRenderBackendView->textBoxesById.find(textBoxId);
    return textBox != recordingRenderBackendView->textBoxesById.end() && textBox->second.text == "Updated text" &&
           textBox->second.bounds.x == 40 && textBox->second.bounds.y == 50;
  }));

  {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    EXPECT_EQ(recordingRenderBackendView->textBoxesById.at(textBoxId).text, "Updated text");
    EXPECT_EQ(recordingRenderBackendView->textBoxesById.at(textBoxId).bounds.x, 40);
    EXPECT_EQ(recordingRenderBackendView->textBoxesById.at(textBoxId).bounds.y, 50);
  }
  screenManager.stop();
  EXPECT_TRUE(recordingRenderBackendView->shutdownWasCalled);
}

TEST(ScreenManagerTest, RejectsDrawingBeforeTheRenderThreadStarts) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 1U);

  EXPECT_THROW(screenManager.drawTextBox({}), std::logic_error);
}

TEST(ScreenManagerTest, RequiresABackendAndANonZeroCommandLimit) {
  EXPECT_THROW(ScreenManager(tests::testActiveDisplay(), nullptr, 1U), std::invalid_argument);
  EXPECT_THROW(ScreenManager(tests::testActiveDisplay(), std::make_unique<tests::RecordingRenderBackend>(), 0U),
               std::invalid_argument);
}

TEST(ScreenManagerTest, SendsFillDeleteClearAndEmergencyScreenCommandsToTheRenderThread) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U);
  screenManager.start();

  const WidgetId textBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Temporary"});
  ASSERT_TRUE(waitForTextBox(*recordingRenderBackendView, textBoxId));
  screenManager.fillArea({{1, 2, 30, 40}, {3, 4, 5}});
  screenManager.deleteTextBox(textBoxId);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->drawnAreas.size() == 1U && recordingRenderBackendView->textBoxesById.empty();
  }));
  screenManager.clear({8, 13, 22});
  screenManager.showErrorScreen({{20, 20, 300, 200}, "Expected failure"});

  const bool allCommandsWereProcessed = tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->drawnAreas.size() == 1U && recordingRenderBackendView->textBoxesById.empty() &&
           recordingRenderBackendView->lastErrorScreenText == "Expected failure";
  });
  screenManager.stop();
  EXPECT_TRUE(allCommandsWereProcessed);
}

TEST(ScreenManagerTest, SendsNormalAndBackgroundJpegCommandsToTheRenderThread) {
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

TEST(ScreenManagerTest, RejectsInvalidImageRequestsBeforeTheyReachTheRenderThread) {
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

TEST(ScreenManagerTest, FailedImageReplacementKeepsTheExistingWidgetAndBackground) {
  tests::TemporaryDirectory imageDirectory;
  const auto                jpegPath = imageDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(jpegPath);
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 8U);
  screenManager.start();
  const auto imageId = screenManager.drawJpegImage({jpegPath, 10, 20, 100U});
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
  EXPECT_TRUE(tests::waitUntil([&] { return originalWidgetPixels.expired() && originalBackgroundPixels.expired(); }));
  screenManager.stop();
}

TEST(ScreenManagerTest, RejectsImageCommandsWhenTheRenderQueueIsFull) {
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

TEST(ScreenManagerTest, ProcessesAnImageDeletionQueuedBeforeTheImageHasBeenDrawn) {
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

TEST(ScreenManagerTest, LetsStartAndStopBeCalledMoreThanOnce) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 2U);
  screenManager.start();
  EXPECT_NO_THROW(screenManager.start());
  screenManager.stop();
  EXPECT_NO_THROW(screenManager.stop());
}

TEST(ScreenManagerTest, ReportsRenderBackendInitializationFailure) {
  ScreenManager screenManager(tests::testActiveDisplay(), std::make_unique<FailingRenderBackend>(true), 2U);
  EXPECT_THROW(screenManager.start(), std::runtime_error);
}

TEST(ScreenManagerTest, ReportsRenderBackendDrawingFailure) {
  ScreenManager screenManager(tests::testActiveDisplay(), std::make_unique<FailingRenderBackend>(false), 2U);
  screenManager.start();
  screenManager.drawTextBox({{0, 0, 10, 10}, "draw"});

  const bool drawingFailureWasReported = tests::waitUntil([&] {
    try {
      screenManager.throwIfRenderThreadFailed();
    } catch (const std::runtime_error &renderThreadFailure) {
      EXPECT_STREQ(renderThreadFailure.what(), "renderer drawing failed");
      return true;
    }
    return false;
  });

  screenManager.stop();
  EXPECT_TRUE(drawingFailureWasReported);
}

} // namespace
} // namespace ui
} // namespace iot
