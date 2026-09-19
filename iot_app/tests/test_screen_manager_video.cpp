#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <fstream>

namespace iot {
namespace ui {
namespace {

std::filesystem::path createTestVideoFile(tests::TemporaryDirectory &temporaryDirectory) {
  const auto videoFilePath = temporaryDirectory.path() / "video.mp4";
  std::ofstream(videoFilePath) << "test video data";
  return videoFilePath;
}

TEST(ScreenManagerVideoTest, ReleasesTheFramebufferDuringPlaybackAndRestartsItAfterwards) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath     = createTestVideoFile(temporaryDirectory);
  auto                      renderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto                     *renderBackendView = renderBackend.get();
  auto                      videoPlayer       = std::make_unique<tests::RecordingExclusiveVideoPlayer>();
  auto                     *videoPlayerView   = videoPlayer.get();
  bool                      renderBackendWasStoppedWhenPlaybackStarted = false;
  // Read the backend state from inside the player call so this test also
  // checks that shutdown happens before playback starts.
  videoPlayer->actionWhenPlaybackStarts = [renderBackendView, &renderBackendWasStoppedWhenPlaybackStarted] {
    std::lock_guard<std::mutex> renderStateLock(renderBackendView->renderStateMutex);
    renderBackendWasStoppedWhenPlaybackStarted = !renderBackendView->isInitialized;
  };
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(renderBackend), 8U, std::move(videoPlayer));
  screenManager.start();
  const WidgetId oldTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Before video"});

  screenManager.playExclusiveVideoAndWait(videoFilePath);

  EXPECT_EQ(videoPlayerView->numberOfPlaybackCalls, 1U);
  EXPECT_EQ(videoPlayerView->lastVideoFilePath, std::filesystem::canonical(videoFilePath));
  ASSERT_TRUE(videoPlayerView->lastActiveDisplay.has_value());
  EXPECT_EQ(videoPlayerView->lastActiveDisplay->display().displayId, tests::testActiveDisplay().display().displayId);
  EXPECT_TRUE(renderBackendWasStoppedWhenPlaybackStarted);
  {
    std::lock_guard<std::mutex> renderStateLock(renderBackendView->renderStateMutex);
    EXPECT_EQ(renderBackendView->numberOfInitializeCalls, 2U);
    EXPECT_EQ(renderBackendView->numberOfShutdownCalls, 1U);
    EXPECT_TRUE(renderBackendView->isInitialized);
  }
  EXPECT_THROW(screenManager.updateTextBox(oldTextBoxId, "Old widget"), std::invalid_argument);
  EXPECT_NO_THROW(screenManager.drawTextBox({{0, 0, 100, 40}, "After video"}));
  screenManager.stop();
}

TEST(ScreenManagerVideoTest, RestartsTheFramebufferWhenPlaybackFails) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath     = createTestVideoFile(temporaryDirectory);
  auto                      renderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto                     *renderBackendView = renderBackend.get();
  auto                      videoPlayer       = std::make_unique<tests::RecordingExclusiveVideoPlayer>();
  videoPlayer->playbackErrorMessage           = "test video playback failed";
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(renderBackend), 8U, std::move(videoPlayer));
  screenManager.start();

  EXPECT_THROW(screenManager.playExclusiveVideoAndWait(videoFilePath), std::runtime_error);

  {
    std::lock_guard<std::mutex> renderStateLock(renderBackendView->renderStateMutex);
    EXPECT_EQ(renderBackendView->numberOfInitializeCalls, 2U);
    EXPECT_EQ(renderBackendView->numberOfShutdownCalls, 1U);
    EXPECT_TRUE(renderBackendView->isInitialized);
  }
  EXPECT_NO_THROW(screenManager.drawTextBox({{0, 0, 100, 40}, "Recovered"}));
  screenManager.stop();
}

TEST(ScreenManagerVideoTest, RejectsInvalidFilesBeforeReleasingTheFramebuffer) {
  tests::TemporaryDirectory temporaryDirectory;
  auto                      renderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto                     *renderBackendView = renderBackend.get();
  auto                      videoPlayer       = std::make_unique<tests::RecordingExclusiveVideoPlayer>();
  auto                     *videoPlayerView   = videoPlayer.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(renderBackend), 8U, std::move(videoPlayer));
  screenManager.start();

  EXPECT_THROW(screenManager.playExclusiveVideoAndWait({}), std::invalid_argument);
  EXPECT_THROW(screenManager.playExclusiveVideoAndWait(temporaryDirectory.path() / "missing.mp4"),
               std::invalid_argument);
  EXPECT_THROW(screenManager.playExclusiveVideoAndWait(temporaryDirectory.path()), std::invalid_argument);

  EXPECT_EQ(videoPlayerView->numberOfPlaybackCalls, 0U);
  {
    std::lock_guard<std::mutex> renderStateLock(renderBackendView->renderStateMutex);
    EXPECT_EQ(renderBackendView->numberOfInitializeCalls, 1U);
    EXPECT_EQ(renderBackendView->numberOfShutdownCalls, 0U);
  }
  screenManager.stop();
}

TEST(ScreenManagerVideoTest, RejectsAnUnreadableFileBeforeReleasingTheFramebuffer) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath = createTestVideoFile(temporaryDirectory);
  std::filesystem::permissions(videoFilePath, std::filesystem::perms::none);

  std::ifstream permissionCheck{videoFilePath, std::ios::binary};
  if (permissionCheck) {
    GTEST_SKIP() << "This test process can read a file with no permissions";
  }

  auto          renderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *renderBackendView = renderBackend.get();
  auto          videoPlayer       = std::make_unique<tests::RecordingExclusiveVideoPlayer>();
  auto         *videoPlayerView   = videoPlayer.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(renderBackend), 8U, std::move(videoPlayer));
  screenManager.start();

  EXPECT_THROW(screenManager.playExclusiveVideoAndWait(videoFilePath), std::invalid_argument);

  EXPECT_EQ(videoPlayerView->numberOfPlaybackCalls, 0U);
  {
    std::lock_guard<std::mutex> renderStateLock(renderBackendView->renderStateMutex);
    EXPECT_EQ(renderBackendView->numberOfInitializeCalls, 1U);
    EXPECT_EQ(renderBackendView->numberOfShutdownCalls, 0U);
  }
  screenManager.stop();
}

TEST(ScreenManagerVideoTest, RequiresTheRenderThreadToBeRunningBeforePlayback) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath   = createTestVideoFile(temporaryDirectory);
  auto                      renderBackend   = std::make_unique<tests::RecordingRenderBackend>();
  auto                      videoPlayer     = std::make_unique<tests::RecordingExclusiveVideoPlayer>();
  auto                     *videoPlayerView = videoPlayer.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(renderBackend), 8U, std::move(videoPlayer));

  EXPECT_THROW(screenManager.playExclusiveVideoAndWait(videoFilePath), std::logic_error);
  EXPECT_EQ(videoPlayerView->numberOfPlaybackCalls, 0U);
}

} // namespace
} // namespace ui
} // namespace iot
