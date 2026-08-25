#include "python_application_manager_test_fixture.h"

namespace iot {
namespace python {
namespace {

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
