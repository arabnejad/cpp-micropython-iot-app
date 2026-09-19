#include "python_application_manager_test_fixture.h"

#include <fstream>
#include <utility>

namespace iot {
namespace python {
namespace {

TEST_F(PythonApplicationManagerTest, ReturnsToPythonAfterVideoPlaybackSoTheApplicationCanRedrawItsScreen) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath = temporaryDirectory.path() / "video.mp4";
  std::ofstream(videoFilePath) << "test video data";
  auto pythonApplicationManager = createApplicationManager();

  std::string pythonSource = "from iot import display\n";
  pythonSource += "video_file_path = '" + videoFilePath.string() + "'\n";
  pythonSource += "display.draw_text_box(0, 0, 100, 40, 'Before video')\n";
  pythonSource += "display.play_video(video_file_path)\n";
  pythonSource += "display.draw_text_box(0, 0, 100, 40, 'After video')\n";
  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Video application", std::move(pythonSource)));

  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  EXPECT_EQ(m_recordingExclusiveVideoPlayerView->numberOfPlaybackCalls, 1U);
  EXPECT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->textBoxesById.size() == 1U &&
           m_recordingRenderBackendView->textBoxesById.begin()->second.text == "After video";
  }));
}

TEST_F(PythonApplicationManagerTest, LetsPythonCatchAPlaybackFailureAndContinueDrawing) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath = temporaryDirectory.path() / "video.mp4";
  std::ofstream(videoFilePath) << "test video data";
  m_recordingExclusiveVideoPlayerView->playbackErrorMessage = "test video playback failed";
  auto pythonApplicationManager                             = createApplicationManager();

  std::string pythonSource = "from iot import display\n";
  pythonSource += "video_file_path = '" + videoFilePath.string() + "'\n";
  pythonSource += "try:\n";
  pythonSource += "    display.play_video(video_file_path)\n";
  pythonSource += "except RuntimeError:\n";
  pythonSource += "    display.draw_text_box(0, 0, 100, 40, 'Recovered')\n";
  pythonSource += "else:\n";
  pythonSource += "    raise AssertionError('Expected playback to fail')\n";
  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Handles video error", std::move(pythonSource)));

  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  EXPECT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->textBoxesById.size() == 1U &&
           m_recordingRenderBackendView->textBoxesById.begin()->second.text == "Recovered";
  }));
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenAfterAnUnhandledPlaybackFailure) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                videoFilePath = temporaryDirectory.path() / "video.mp4";
  std::ofstream(videoFilePath) << "test video data";
  m_recordingExclusiveVideoPlayerView->playbackErrorMessage = "test video playback failed";
  auto pythonApplicationManager                             = createApplicationManager();

  std::string pythonSource = "from iot import display\n";
  pythonSource += "video_file_path = '" + videoFilePath.string() + "'\n";
  pythonSource += "display.play_video(video_file_path)\n";
  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Video failure", std::move(pythonSource)));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("test video playback failed"), std::string::npos);
}

} // namespace
} // namespace python
} // namespace iot
