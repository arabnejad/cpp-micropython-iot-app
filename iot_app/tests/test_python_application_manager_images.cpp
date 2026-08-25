#include "python_application_manager_test_fixture.h"

#include "test_jpeg_file.h"

#include <fstream>

namespace iot {
namespace python {
namespace {

TEST_F(PythonApplicationManagerTest, LetsPythonCatchADownloadTimeoutWithoutStoppingTheApplication) {
  auto pythonApplicationManager         = createApplicationManager();
  m_fileDownloader.downloadErrorMessage = "File download timed out";

  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Handles timeout", "from iot import network\n"
                                                 "try:\n"
                                                 "    network.download_file('https://example.com/image.jpg')\n"
                                                 "except RuntimeError as error:\n"
                                                 "    assert 'timed out' in str(error)\n"
                                                 "else:\n"
                                                 "    raise AssertionError('Expected a timeout')\n"));

  EXPECT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
}

TEST_F(PythonApplicationManagerTest, ShowsAnUnhandledDownloadTimeoutOnTheEmergencyScreen) {
  auto pythonApplicationManager         = createApplicationManager();
  m_fileDownloader.downloadErrorMessage = "File download timed out";

  const auto activationResult = pythonApplicationManager.activateExternalApplication(createPythonApplication(
      "Download failed", "from iot import network\nnetwork.download_file('https://example.com/image.jpg')\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("RuntimeError: File download timed out"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, ReleasesOldWidgetBackgroundAndCachedPixelsWhenAnotherApplicationStarts) {
  tests::TemporaryDirectory imageDirectory;
  m_fileDownloader.downloadedFile.filePath = imageDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(m_fileDownloader.downloadedFile.filePath);
  auto       pythonApplicationManager = createApplicationManager();
  const auto firstActivation          = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("With images", "from iot import display, network\n"
                                                               "download = network.download_file('https://example.com/image.jpg')\n"
                                                               "display.draw_image(download['path'], 10, 20)\n"
                                                               "display.set_background_image(download['path'], mode='center')\n"));
  ASSERT_TRUE(firstActivation.externalApplicationIsRunning) << firstActivation.failureReason;
  ASSERT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return !m_recordingRenderBackendView->jpegImagesById.empty() &&
           m_recordingRenderBackendView->backgroundJpegImage.has_value();
  }));
  std::weak_ptr<const ui::DecodedJpegImage> oldWidgetPixels;
  std::weak_ptr<const ui::DecodedJpegImage> oldBackgroundPixels;
  {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    oldWidgetPixels     = m_recordingRenderBackendView->jpegImagesById.begin()->second.decodedImage;
    oldBackgroundPixels = m_recordingRenderBackendView->backgroundJpegImage->decodedImage;
  }

  ASSERT_TRUE(
      pythonApplicationManager.activateExternalApplication(createPythonApplication("Without images", "value = 1\n"))
          .externalApplicationIsRunning);

  EXPECT_TRUE(tests::waitUntil([&] { return oldWidgetPixels.expired() && oldBackgroundPixels.expired(); }));
  EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 2U);
}

TEST_F(PythonApplicationManagerTest, ShowsACorruptJpegErrorAndAcceptsTheNextApplication) {
  tests::TemporaryDirectory imageDirectory;
  m_fileDownloader.downloadedFile.filePath = imageDirectory.path() / "corrupt.jpg";
  std::ofstream(m_fileDownloader.downloadedFile.filePath) << "not a JPEG";
  auto pythonApplicationManager = createApplicationManager();

  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Corrupt image", "from iot import display, network\n"
                                               "download = network.download_file('https://example.com/image.jpg')\n"
                                               "display.draw_image(download['path'], 0, 0)\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("JPEG"), std::string::npos);
  EXPECT_TRUE(
      pythonApplicationManager.activateExternalApplication(createPythonApplication("Healthy app", "value = 1\n"))
          .externalApplicationIsRunning);
}

TEST_F(PythonApplicationManagerTest, ReleasesImagePixelsWhenAScheduledImageChangeFails) {
  tests::TemporaryDirectory imageDirectory;
  m_fileDownloader.downloadedFile.filePath = imageDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(m_fileDownloader.downloadedFile.filePath);
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Later image error", "from iot import display, network, scheduler\n"
                                                                   "download = network.download_file('https://example.com/image.jpg')\n"
                                                                   "image_id = display.draw_image(download['path'], 0, 0)\n"
                                                                   "def change_image():\n"
                                                                   "    display.update_image(image_id, download['path'] + '.missing')\n"
                                                                   "scheduler.every(milliseconds=1, callback=change_image)\n"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  ASSERT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return !m_recordingRenderBackendView->jpegImagesById.empty();
  }));
  std::weak_ptr<const ui::DecodedJpegImage> oldPixels;
  {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    oldPixels = m_recordingRenderBackendView->jpegImagesById.begin()->second.decodedImage;
  }

  ASSERT_TRUE(tests::waitUntil([&] {
    pythonApplicationManager.runScheduledCallbacks();
    return pythonApplicationManager.state() == ApplicationState::EmergencyScreen;
  }));

  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("JPEG image file does not exist"), std::string::npos);
  EXPECT_FALSE(pythonApplicationManager.timeUntilNextScheduledCallback().has_value());
  EXPECT_TRUE(tests::waitUntil([&] { return oldPixels.expired(); }));
}

TEST_F(PythonApplicationManagerTest, LetsPythonUpdateMoveScaleAndDeleteImagesAndChooseBackgroundModes) {
  tests::TemporaryDirectory imageDirectory;
  m_fileDownloader.downloadedFile.filePath = imageDirectory.path() / "picture.jpg";
  tests::writeTestJpegFile(m_fileDownloader.downloadedFile.filePath);
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Image API", "from iot import display, network\n"
                                                           "download = network.download_file('https://example.com/image.jpg')\n"
                                                           "path = download['path']\n"
                                                           "image_id = display.draw_image(path, 10, 20)\n"
                                                           "display.update_image(image_id, path)\n"
                                                           "display.move_image(image_id, 30, 40)\n"
                                                           "display.set_image_scale(image_id, 50)\n"
                                                           "for mode in ('center', 'fit', 'tile'):\n"
                                                           "    display.set_background_image(path, mode=mode)\n"
                                                           "display.clear_background_image()\n"
                                                           "display.delete_image(image_id)\n"
                                                           "try:\n"
                                                           "    display.move_image(image_id, 0, 0)\n"
                                                           "except RuntimeError:\n"
                                                           "    pass\n"
                                                           "else:\n"
                                                           "    raise AssertionError('Deleted image ID should be rejected')\n"
                                                           "try:\n"
                                                           "    display.set_background_image(path, mode='unknown')\n"
                                                           "except ValueError:\n"
                                                           "    pass\n"
                                                           "else:\n"
                                                           "    raise AssertionError('Unknown background mode should be rejected')\n"
                                                           "display.draw_image(path, 75, 85, scale_percent=50)\n"));

  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  EXPECT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    if (m_recordingRenderBackendView->jpegImagesById.size() != 1U) {
      return false;
    }
    const auto &remainingImage = m_recordingRenderBackendView->jpegImagesById.begin()->second;
    return remainingImage.x == 75 && remainingImage.y == 85 && remainingImage.decodedImage->width == 8U &&
           remainingImage.decodedImage->height == 6U && !m_recordingRenderBackendView->backgroundJpegImage.has_value();
  }));
  EXPECT_NO_THROW(m_screenManager.throwIfRenderThreadFailed());
}

} // namespace
} // namespace python
} // namespace iot
