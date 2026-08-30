#include "iot/python/python_application_manager.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

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

  PythonApplicationManager createApplicationManager(std::size_t connectedDisplayCount = 1U) {
    return PythonApplicationManager(m_screenManager, tests::testActiveDisplay(), connectedDisplayCount,
                                    m_systemInformationProvider, 256U * 1024U);
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
};

TEST_F(PythonApplicationManagerTest, StartsStopsAndRestartsTheShippedDefaultApplication) {
  auto                    pythonApplicationManager = createApplicationManager();
  const PythonApplication defaultApplication       = createPythonApplication("Default", "value = 1\n");

  EXPECT_FALSE(pythonApplicationManager.timeUntilNextScheduledCallback().has_value());
  pythonApplicationManager.runScheduledCallbacks();
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::Stopped);

  pythonApplicationManager.startDefaultApplication(defaultApplication);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::DefaultApplication);
  EXPECT_EQ(pythonApplicationManager.activeScreenName(), "Default");

  pythonApplicationManager.stop();
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::Stopped);
  EXPECT_TRUE(pythonApplicationManager.activeScreenName().empty());

  pythonApplicationManager.startDefaultApplication(defaultApplication);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::DefaultApplication);
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

  const auto activationResult = pythonApplicationManager.activateExternalApplication(createPythonApplication(
      "Native module test", "import iot\n"
                            "width, height = iot.display.size()\n"
                            "assert (width, height) == (1920, 1080)\n"
                            "assert iot.system.uptime_seconds() == 99\n"
                            "iot.display.draw_text_box(10, 20, 200, 40, 'Created by Python')\n"));

  ASSERT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
  ASSERT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return !m_recordingRenderBackendView->textBoxesById.empty();
  }));

  std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
  EXPECT_EQ(m_recordingRenderBackendView->textBoxesById.begin()->second.text, "Created by Python");
}

TEST_F(PythonApplicationManagerTest, LetsPythonUseEveryDisplayAndSystemFunction) {
  auto pythonApplicationManager = createApplicationManager(2U);

  const auto activationResult = pythonApplicationManager.activateExternalApplication(createPythonApplication(
      "Native module test", "import iot\n"
                            "display_information = iot.display.information()\n"
                            "assert display_information['connected_display_count'] == 2\n"
                            "assert display_information['connector_name'] == 'HDMI-A-1'\n"
                            "assert display_information['width'] == 1920\n"
                            "assert display_information['height'] == 1080\n"
                            "assert display_information['refresh_rate_hz'] == 60\n"
                            "text_box = iot.display.draw_text_box(10, 20, 300, 80, 'Initial text', "
                            "text_color=(10, 255, 255), background_opacity=255, border_width=2, font_size=24)\n"
                            "iot.display.update_text_box(text_box, 'Updated text')\n"
                            "iot.display.move_text_box(text_box, 30, 40)\n"
                            "iot.display.fill_area(0, 0, 20, 20, color=(1, 2, 3))\n"
                            "iot.display.delete_text_box(text_box)\n"
                            "iot.display.clear(color=(8, 13, 22))\n"
                            "system_information = iot.system.information()\n"
                            "assert system_information['hostname'] == 'test-device'\n"
                            "assert system_information['uptime_seconds'] == 42\n"
                            "resources = iot.system.resources()\n"
                            "assert resources['logical_cpu_count'] == 4\n"
                            "interfaces = iot.system.interfaces()\n"
                            "assert interfaces['i2c'] == 0\n"
                            "devices = iot.system.devices()\n"
                            "assert devices['usb'] == 0\n"
                            "application_information = iot.system.app_information()\n"
                            "assert application_information['application_name'] == 'Native module test'\n"
                            "network_interfaces = iot.system.network_interfaces()\n"
                            "assert network_interfaces[0]['name'] == 'eth0'\n"
                            "assert network_interfaces[0]['connected'] is True\n"
                            "assert network_interfaces[0]['ipv4_address'] == '192.0.2.10'\n"
                            "assert network_interfaces[0]['speed_megabits_per_second'] == 1000\n"
                            "assert iot.system.uptime_seconds() == 99\n"
                            "assert len(iot.system.current_time()) == 19\n"));

  EXPECT_TRUE(activationResult.externalApplicationIsRunning) << activationResult.failureReason;
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenTheDefaultApplicationFailsToStart) {
  auto pythonApplicationManager = createApplicationManager();

  pythonApplicationManager.startDefaultApplication(
      createPythonApplication("Broken default", "raise RuntimeError('broken default')\n"));

  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("Broken default"), std::string::npos);
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

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenReadingSystemInformationFails) {
  ThrowingSystemInformationProvider throwingSystemInformationProvider;
  PythonApplicationManager          pythonApplicationManager(m_screenManager, tests::testActiveDisplay(), 1U,
                                                             throwingSystemInformationProvider, 256U * 1024U);

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
  EXPECT_THROW(static_cast<void>(PythonApplicationManager(m_screenManager, tests::testActiveDisplay(), 1U,
                                                          m_systemInformationProvider, 0U)),
               std::invalid_argument);
}

} // namespace
} // namespace python
} // namespace iot
