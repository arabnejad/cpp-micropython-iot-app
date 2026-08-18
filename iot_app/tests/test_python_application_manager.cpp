#include "iot/python/python_application_manager.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"
#include "test_jpeg_file.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace iot {
namespace python {
namespace {

PythonApplication createPythonApplication(std::string applicationName, std::string sourceCode) {
  PythonApplication pythonApplication;
  pythonApplication.applicationId   = "test-application";
  pythonApplication.applicationName = std::move(applicationName);
  pythonApplication.entryPointPath  = "main.py";
  pythonApplication.sourceCode      = std::move(sourceCode);
  return pythonApplication;
}

class ThrowingSystemInformationProvider final : public system::ISystemInformationProvider {
public:
  system::SystemInformation readSystemInformation() const override {
    throw std::runtime_error("system snapshot failed");
  }

  std::string readCurrentLocalTime() const override {
    return "2000-01-01 00:00:00";
  }

  std::uint64_t readUptimeSeconds() const override {
    return 0U;
  }

  std::vector<system::NetworkInterfaceInformation> readNetworkInterfaces() const override {
    return {};
  }
};

class PythonApplicationManagerTest : public ::testing::Test {
protected:
  PythonApplicationManagerTest()
      : m_recordingRenderBackend(std::make_unique<tests::RecordingRenderBackend>()),
        m_recordingRenderBackendView(m_recordingRenderBackend.get()),
        m_screenManager(tests::testActiveDisplay(), std::move(m_recordingRenderBackend), 32U) {}

  void SetUp() override {
    m_screenManager.start();
  }

  void TearDown() override {
    m_screenManager.stop();
  }

  PythonApplicationManager
  createApplicationManager(std::vector<display::DisplayInfo> connectedDisplays = tests::testConnectedDisplays()) {
    return PythonApplicationManager(m_screenManager, tests::testActiveDisplay(), std::move(connectedDisplays),
                                    m_systemInformationProvider, m_fileDownloader, 256U * 1024U);
  }

  bool waitForEmergencyScreen() {
    return tests::waitUntil([this] {
      std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
      return !m_recordingRenderBackendView->lastErrorScreenText.empty();
    });
  }

  std::string emergencyScreenText() {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->lastErrorScreenText;
  }

  std::unique_ptr<tests::RecordingRenderBackend> m_recordingRenderBackend;
  tests::RecordingRenderBackend                 *m_recordingRenderBackendView;
  ui::ScreenManager                              m_screenManager;
  tests::TestSystemInformationProvider           m_systemInformationProvider;
  tests::TestFileDownloader                      m_fileDownloader;
};

TEST_F(PythonApplicationManagerTest, StartsStopsAndRestartsTheShippedDefaultApplication) {
  auto                    pythonApplicationManager = createApplicationManager();
  const PythonApplication defaultApplication       = createPythonApplication("Default", "value = 1\n");

  EXPECT_FALSE(pythonApplicationManager.timeUntilNextScheduledCallback().has_value());
  pythonApplicationManager.runScheduledCallbacks();
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::Stopped);

  pythonApplicationManager.startDefaultApplication(defaultApplication);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::DefaultApplication);
  EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 1U);
  EXPECT_EQ(pythonApplicationManager.activeScreenName(), "Default");

  pythonApplicationManager.stop();
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::Stopped);
  EXPECT_TRUE(pythonApplicationManager.activeScreenName().empty());

  pythonApplicationManager.startDefaultApplication(defaultApplication);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::DefaultApplication);
  EXPECT_EQ(m_fileDownloader.numberOfClearCalls, 2U);
}

TEST_F(PythonApplicationManagerTest, ReplacesTheRunningApplicationWithANewInterpreter) {
  auto pythonApplicationManager = createApplicationManager();

  const auto firstActivation = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("First external app", "value = 1\n"));
  const auto secondActivation = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Second external app", "value = 2\n"));

  EXPECT_TRUE(firstActivation.externalApplicationIsRunning);
  EXPECT_TRUE(secondActivation.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::ExternalApplication);
  EXPECT_EQ(pythonApplicationManager.activeScreenName(), "Second external app");
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenDefaultApplicationDownloadCleanupFails) {
  auto pythonApplicationManager      = createApplicationManager();
  m_fileDownloader.clearErrorMessage = "download cleanup failed";

  EXPECT_NO_THROW(pythonApplicationManager.startDefaultApplication(createPythonApplication("Default", "value = 1\n")));

  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("download cleanup failed"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, StopsTheOldInterpreterWhenCleanupFailsAndCanStartAnotherApplicationLater) {
  auto pythonApplicationManager = createApplicationManager();
  ASSERT_TRUE(
      pythonApplicationManager
          .activateExternalApplication(createPythonApplication(
              "Old app",
              "from iot import scheduler\ndef tick():\n    pass\nscheduler.every(milliseconds=1000, callback=tick)\n"))
          .externalApplicationIsRunning);
  ASSERT_TRUE(pythonApplicationManager.timeUntilNextScheduledCallback().has_value());
  m_fileDownloader.clearErrorMessage = "download cleanup failed";

  const auto failedActivation =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("New app", "value = 2\n"));

  EXPECT_FALSE(failedActivation.externalApplicationIsRunning);
  EXPECT_EQ(failedActivation.failureReason, "download cleanup failed");
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  EXPECT_FALSE(pythonApplicationManager.timeUntilNextScheduledCallback().has_value());
  ASSERT_TRUE(waitForEmergencyScreen());

  m_fileDownloader.clearErrorMessage.clear();
  EXPECT_TRUE(pythonApplicationManager.activateExternalApplication(createPythonApplication("Retry", "value = 3\n"))
                  .externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::ExternalApplication);
}

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

TEST_F(PythonApplicationManagerTest, LetsPythonUseDisplayAndSystemModulesThroughTheApplicationContext) {
  auto pythonApplicationManager = createApplicationManager();

  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Native module test", "from iot import display, system\n"
                                                    "width, height = display.size()\n"
                                                    "assert (width, height) == (1920, 1080)\n"
                                                    "assert system.uptime_seconds() == 99\n"
                                                    "display.draw_text_box(10, 20, 200, 40, 'Created by Python')\n"));

  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  ASSERT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return !m_recordingRenderBackendView->textBoxesById.empty();
  }));

  std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
  EXPECT_EQ(m_recordingRenderBackendView->textBoxesById.begin()->second.text, "Created by Python");
}

TEST_F(PythonApplicationManagerTest, LetsPythonUseEveryDisplayAndSystemFunction) {
  auto connectedDisplays                = tests::testConnectedDisplays();
  auto secondDisplay                    = connectedDisplays.front();
  secondDisplay.displayId.connectorName = "HDMI-A-2";
  secondDisplay.displayId.connectorId   = 2U;
  secondDisplay.manufacturer            = "ALT";
  secondDisplay.model                   = "Second monitor";
  secondDisplay.serialNumber            = "MONITOR-2";
  secondDisplay.physicalWidthMm         = 520U;
  secondDisplay.physicalHeightMm        = 290U;
  secondDisplay.currentMode.reset();
  secondDisplay.supportedModes.front().preferred  = false;
  secondDisplay.supportedModes.front().interlaced = true;
  connectedDisplays.push_back(std::move(secondDisplay));
  auto pythonApplicationManager = createApplicationManager(std::move(connectedDisplays));

  const auto activationResult = pythonApplicationManager.activateExternalApplication(createPythonApplication(
      "Native module test", "from iot import display, network, system\n"
                            "monitors = display.monitors()\n"
                            "assert len(monitors) == 2\n"
                            "assert monitors[0]['connector_name'] == 'HDMI-A-1'\n"
                            "assert monitors[0]['manufacturer'] == 'TST'\n"
                            "assert monitors[0]['model'] == 'Test monitor'\n"
                            "assert monitors[0]['serial_number'] == 'MONITOR-1'\n"
                            "assert monitors[0]['physical_width_mm'] == 600\n"
                            "assert monitors[0]['physical_height_mm'] == 340\n"
                            "assert monitors[0]['active'] is True\n"
                            "assert monitors[0]['current_mode']['width'] == 1920\n"
                            "assert monitors[0]['current_mode']['height'] == 1080\n"
                            "assert monitors[0]['current_mode']['refresh_rate_hz'] == 60\n"
                            "assert monitors[0]['supported_modes'][0]['name'] == '1920x1080'\n"
                            "assert monitors[0]['supported_modes'][0]['preferred'] is True\n"
                            "assert monitors[0]['supported_modes'][0]['interlaced'] is False\n"
                            "assert monitors[1]['connector_name'] == 'HDMI-A-2'\n"
                            "assert monitors[1]['active'] is False\n"
                            "assert monitors[1]['current_mode'] is None\n"
                            "assert monitors[1]['supported_modes'][0]['interlaced'] is True\n"
                            "active_monitor = display.active_monitor()\n"
                            "assert active_monitor['connector_name'] == 'HDMI-A-1'\n"
                            "text_box = display.draw_text_box(10, 20, 300, 80, 'Initial text', "
                            "text_color=(10, 255, 255), background_opacity=255, border_width=2, font_size=24)\n"
                            "display.update_text_box(text_box, 'Updated text')\n"
                            "display.move_text_box(text_box, 30, 40)\n"
                            "display.fill_area(0, 0, 20, 20, color=(1, 2, 3))\n"
                            "display.delete_text_box(text_box)\n"
                            "display.clear(color=(8, 13, 22))\n"
                            "system_information = system.information()\n"
                            "assert system_information['hostname'] == 'test-device'\n"
                            "assert system_information['uptime_seconds'] == 42\n"
                            "resources = system.resources()\n"
                            "assert resources['logical_cpu_count'] == 4\n"
                            "interfaces = system.interfaces()\n"
                            "assert interfaces['i2c'] == 0\n"
                            "devices = system.devices()\n"
                            "assert devices['usb'] == 0\n"
                            "application_information = system.app_information()\n"
                            "assert application_information['application_name'] == 'Native module test'\n"
                            "network_interfaces = system.network_interfaces()\n"
                            "assert network_interfaces[0]['name'] == 'eth0'\n"
                            "assert network_interfaces[0]['connected'] is True\n"
                            "assert network_interfaces[0]['ipv4_address'] == '192.0.2.10'\n"
                            "assert network_interfaces[0]['speed_megabits_per_second'] == 1000\n"
                            "downloaded_file = network.download_file('https://example.com/image.jpg', "
                            "expected_sha256='expected')\n"
                            "assert downloaded_file['path'] == '/tmp/test-download.jpg'\n"
                            "assert downloaded_file['sha256'] == 'test-sha256'\n"
                            "assert downloaded_file['size_bytes'] == 123\n"
                            "assert downloaded_file['content_type'] == 'image/jpeg'\n"
                            "assert downloaded_file['loaded_from_cache'] is False\n"
                            "assert system.uptime_seconds() == 99\n"
                            "assert system.current_time() == '2000-01-01 00:00:00'\n"));

  EXPECT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  EXPECT_EQ(m_fileDownloader.lastRequest.url, "https://example.com/image.jpg");
  EXPECT_EQ(m_fileDownloader.lastRequest.expectedSha256, "expected");
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenTheDefaultApplicationFailsToStart) {
  auto pythonApplicationManager = createApplicationManager();

  pythonApplicationManager.startDefaultApplication(
      createPythonApplication("Broken default", "raise RuntimeError('broken default')\n"));

  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("Broken default"), std::string::npos);
  EXPECT_NE(emergencyScreenText().find("Time: 2000-01-01 00:00:00"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, KeepsThePythonErrorWhenTheEmergencyScreenCannotReadTheLocalTime) {
  auto pythonApplicationManager                               = createApplicationManager();
  m_systemInformationProvider.failWhenReadingCurrentLocalTime = true;

  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Broken external", "raise RuntimeError('original Python error')\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("Time: Time unavailable"), std::string::npos);
  EXPECT_NE(emergencyScreenText().find("original Python error"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, LetsPythonCatchInvalidTextBoxRequestsAndContinueDrawing) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Handles invalid text boxes", R"python(
from iot import display

for width, height in ((0, 40), (-1, 40), (100, 0), (100, -1)):
    try:
        display.draw_text_box(0, 0, width, height, "Invalid size")
    except RuntimeError as error:
        assert "positive width and height" in str(error)
    else:
        raise AssertionError("Invalid size was accepted")

try:
    display.update_text_box(999, "Unknown text box")
except RuntimeError:
    pass
else:
    raise AssertionError("Unknown ID was accepted for update")

try:
    display.move_text_box(999, 10, 20)
except RuntimeError:
    pass
else:
    raise AssertionError("Unknown ID was accepted for move")

try:
    display.delete_text_box(999)
except RuntimeError:
    pass
else:
    raise AssertionError("Unknown ID was accepted for deletion")

display.draw_text_box(0, 0, 300, 80, "Still running")
)python"));

  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::ExternalApplication);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->textBoxesById.size() == 1U &&
           m_recordingRenderBackendView->textBoxesById.begin()->second.text == "Still running";
  }));
  EXPECT_NO_THROW(m_screenManager.throwIfRenderThreadFailed());
  EXPECT_TRUE(emergencyScreenText().empty());
}

TEST_F(PythonApplicationManagerTest, ShowsInvalidTextBoxDimensionsOnTheEmergencyScreenAndCanStartAnotherApp) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Invalid dimensions", "from iot import display\n"
                                                                    "display.draw_text_box(0, 0, 0, 40, 'Invalid width')\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("positive width and height"), std::string::npos);
  EXPECT_NO_THROW(m_screenManager.throwIfRenderThreadFailed());

  const auto recoveryResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Valid replacement", "from iot import display\n"
                                                   "display.draw_text_box(0, 0, 300, 80, 'Recovered')\n"));
  ASSERT_TRUE(recoveryResult.externalApplicationIsRunning) << recoveryResult.failureReason;
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->lastErrorScreenText.empty() &&
           m_recordingRenderBackendView->textBoxesById.size() == 1U &&
           m_recordingRenderBackendView->textBoxesById.begin()->second.text == "Recovered";
  }));
  EXPECT_NO_THROW(m_screenManager.throwIfRenderThreadFailed());
}

TEST_F(PythonApplicationManagerTest, ShowsADeletedTextBoxErrorFromACallbackWithoutStoppingTheRenderer) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("Uses a deleted text box", R"python(
from iot import display, scheduler

text_box_id = display.draw_text_box(0, 0, 300, 80, "Temporary")
display.delete_text_box(text_box_id)

def update_deleted_box():
    display.update_text_box(text_box_id, "Already deleted")

scheduler.every(1, update_deleted_box)
)python"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;

  ASSERT_TRUE(tests::waitUntil([&] {
    pythonApplicationManager.runScheduledCallbacks();
    return pythonApplicationManager.state() == ApplicationState::EmergencyScreen;
  }));
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("text-box widget does not exist"), std::string::npos);
  EXPECT_NO_THROW(m_screenManager.throwIfRenderThreadFailed());
}

TEST_F(PythonApplicationManagerTest, AReplacementApplicationCannotUseThePreviousApplicationsTextBoxId) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto firstActivationResult    = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("First application", "from iot import display\n"
                                                         "display.draw_text_box(0, 0, 300, 80, 'First application')\n"));
  ASSERT_TRUE(firstActivationResult.externalApplicationIsRunning);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->textBoxesById.size() == 1U;
  }));
  ui::WidgetId previousTextBoxId;
  {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    previousTextBoxId = m_recordingRenderBackendView->textBoxesById.begin()->first;
  }
  const std::string replacementSource = "from iot import display\n"
                                        "try:\n"
                                        "    display.update_text_box(" +
                                        std::to_string(previousTextBoxId) +
                                        ", 'Stale ID')\n"
                                        "except RuntimeError:\n"
                                        "    pass\n"
                                        "else:\n"
                                        "    raise AssertionError('Previous application ID was accepted')\n"
                                        "display.draw_text_box(0, 0, 300, 80, 'Replacement application')\n";

  const auto replacementResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Replacement application", replacementSource));

  ASSERT_TRUE(replacementResult.externalApplicationIsRunning) << replacementResult.failureReason;
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->textBoxesById.size() == 1U &&
           m_recordingRenderBackendView->textBoxesById.begin()->second.text == "Replacement application";
  }));
  EXPECT_NO_THROW(m_screenManager.throwIfRenderThreadFailed());
}

TEST_F(PythonApplicationManagerTest, KeepsTheNewestPartOfALongTracebackOnTheEmergencyScreen) {
  const std::string longErrorMessage(3000U, 'x');
  auto              pythonApplicationManager = createApplicationManager();

  pythonApplicationManager.startDefaultApplication(
      createPythonApplication("Long failure", "raise RuntimeError('" + longErrorMessage + " newest-part')\n"));

  ASSERT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("[Earlier traceback text omitted]"), std::string::npos);
  EXPECT_NE(emergencyScreenText().find("newest-part"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenAnExternalApplicationFailsToStart) {
  auto pythonApplicationManager = createApplicationManager();

  const auto activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Broken external", "raise RuntimeError('external broke')\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(activationResult.failureReason, "Python raised an exception while starting the external application");
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("external broke"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenAScheduledCallbackFails) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("External with timer", "import iot\n"
                                                                     "def fail_later():\n"
                                                                     "    raise RuntimeError('timer failed')\n"
                                                                     "iot.scheduler.every(milliseconds=1, callback=fail_later)\n"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning);

  // The callback cannot run until its one-millisecond interval has passed.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  pythonApplicationManager.runScheduledCallbacks();

  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("timer failed"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, ReportsAndRunsTheNextScheduledCallback) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("External with timer", "import iot\n"
                                                                     "callback_count = 0\n"
                                                                     "def count_callback():\n"
                                                                     "    global callback_count\n"
                                                                     "    callback_count += 1\n"
                                                                     "iot.scheduler.every(milliseconds=1, callback=count_callback)\n"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning);

  const auto timeUntilCallback = pythonApplicationManager.timeUntilNextScheduledCallback();
  ASSERT_TRUE(timeUntilCallback.has_value());
  EXPECT_LE(*timeUntilCallback, std::chrono::milliseconds(1));

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  pythonApplicationManager.runScheduledCallbacks();

  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::ExternalApplication);
  EXPECT_EQ(pythonApplicationManager.activeScreenName(), "External with timer");
}

TEST_F(PythonApplicationManagerTest, CountsTimeSpentInsideACallbackBeforeChoosingTheNextWait) {
  m_fileDownloader.downloadDelay      = std::chrono::milliseconds(40);
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("External with slow timer", R"python(
from iot import network, scheduler

def download_file():
    network.download_file("https://example.com/status.json")

scheduler.every(milliseconds=10, callback=download_file)
)python"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning);

  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  pythonApplicationManager.runScheduledCallbacks();

  EXPECT_EQ(m_fileDownloader.numberOfDownloadCalls, 1U);
  EXPECT_EQ(pythonApplicationManager.timeUntilNextScheduledCallback(), std::chrono::milliseconds::zero());

  // The callback took longer than several intervals, but one scheduler update
  // still runs it only once.
  pythonApplicationManager.runScheduledCallbacks();
  EXPECT_EQ(m_fileDownloader.numberOfDownloadCalls, 2U);
}

TEST_F(PythonApplicationManagerTest, CountsTimeSpentInOneCallbackTowardsAnotherTimersDeadline) {
  /*
   * 0 ms       about 5 ms                   about 205 ms             1000 ms
   * |---------------|----------------------------|-----------------------|
   * Start both      Run the 1 ms timer.           Slow callback ends.     Other
   * timers          It cancels itself and         About 795 ms remain     timer
   *                 takes 200 ms to download.     for the other timer.    is due
   *
   * The other timer had about 995 ms left when the callback started. The
   * 200 ms spent inside that callback must reduce its next wait to about
   * 795 ms. Without that adjustment, the runtime would report about 995 ms.
   */
  m_fileDownloader.downloadDelay      = std::chrono::milliseconds(200);
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("External with two timers", R"python(
from iot import network, scheduler

def slow_callback():
    scheduler.cancel(slow_timer_id)
    network.download_file("https://example.com/status.json")

def other_callback():
    pass

slow_timer_id = scheduler.every(milliseconds=1, callback=slow_callback)
scheduler.every(milliseconds=1000, callback=other_callback)
)python"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  pythonApplicationManager.runScheduledCallbacks();

  ASSERT_EQ(m_fileDownloader.numberOfDownloadCalls, 1U);
  const auto timeUntilOtherTimer = pythonApplicationManager.timeUntilNextScheduledCallback();
  ASSERT_TRUE(timeUntilOtherTimer.has_value());

  // The 900 ms limit allows normal test timing differences, but still fails
  // if the manager ignores the 200 ms callback.
  EXPECT_LT(*timeUntilOtherTimer, std::chrono::milliseconds(900));
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenReadingSystemInformationFails) {
  ThrowingSystemInformationProvider throwingSystemInformationProvider;
  PythonApplicationManager          pythonApplicationManager(m_screenManager, tests::testActiveDisplay(),
                                                             tests::testConnectedDisplays(), throwingSystemInformationProvider,
                                                             m_fileDownloader, 256U * 1024U);

  const auto activationResult =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("External", "value = 2\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(activationResult.failureReason, "system snapshot failed");
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("system snapshot failed"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, RejectsAnEmptyDefaultApplication) {
  auto pythonApplicationManager = createApplicationManager();

  EXPECT_THROW(pythonApplicationManager.startDefaultApplication(PythonApplication{}), std::invalid_argument);
}

TEST_F(PythonApplicationManagerTest, RejectsAZeroByteMicroPythonHeap) {
  EXPECT_THROW(static_cast<void>(PythonApplicationManager(m_screenManager, tests::testActiveDisplay(),
                                                          tests::testConnectedDisplays(), m_systemInformationProvider,
                                                          m_fileDownloader, 0U)),
               std::invalid_argument);
}

} // namespace
} // namespace python
} // namespace iot
