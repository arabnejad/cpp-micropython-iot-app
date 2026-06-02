#include "iot/python/python_application_runner.h"
#include "iot/python/micropython_application_context.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

namespace iot {
namespace python {
namespace {

PythonApplication createPythonApplication(std::string sourceCode) {
  PythonApplication pythonApplication;
  pythonApplication.applicationId   = "native-module-test";
  pythonApplication.applicationName = "Native module test";
  pythonApplication.entryPointPath  = "main.py";
  pythonApplication.sourceCode      = std::move(sourceCode);
  return pythonApplication;
}

class PythonApplicationRunnerTest : public ::testing::Test {
protected:
  PythonApplicationRunnerTest()
      : recordingRenderBackend_(std::make_unique<tests::RecordingRenderBackend>()),
        recordingRenderBackendView_(recordingRenderBackend_.get()),
        screenManager_(tests::testActiveDisplay(), std::move(recordingRenderBackend_), 32U),
        pythonApplicationRunner_(screenManager_, tests::testActiveDisplay(), displayManager_,
                                 systemInformationProvider_, 256U * 1024U) {}

  void SetUp() override {
    screenManager_.start();
  }

  void TearDown() override {
    pythonApplicationRunner_.stop();
    screenManager_.stop();
  }

  std::unique_ptr<tests::RecordingRenderBackend> recordingRenderBackend_;
  tests::RecordingRenderBackend                 *recordingRenderBackendView_;
  ui::ScreenManager                              screenManager_;
  tests::TestDisplayManager                      displayManager_;
  tests::TestSystemInformationProvider           systemInformationProvider_;
  PythonApplicationRunner                        pythonApplicationRunner_;
};

TEST_F(PythonApplicationRunnerTest, LetsPythonUseTheDisplayAndSystemModulesThroughTheApplicationContext) {
  const PythonExecutionResult applicationStartupResult = pythonApplicationRunner_.start(
      createPythonApplication("import iot\n"
                              "width, height = iot.display.size()\n"
                              "assert (width, height) == (1920, 1080)\n"
                              "assert iot.system.uptime_seconds() == 99\n"
                              "iot.display.draw_text_box(10, 20, 200, 40, 'Created by Python')\n"));

  ASSERT_TRUE(applicationStartupResult.succeeded) << applicationStartupResult.traceback;
  ASSERT_TRUE(tests::waitUntil([this] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView_->renderStateMutex);
    return !recordingRenderBackendView_->textBoxesById.empty();
  }));

  std::lock_guard<std::mutex> lock(recordingRenderBackendView_->renderStateMutex);
  EXPECT_EQ(recordingRenderBackendView_->textBoxesById.begin()->second.text, "Created by Python");
}

TEST_F(PythonApplicationRunnerTest, ExercisesEveryDisplayAndSystemApiWithControlledCplusplusServices) {
  const PythonExecutionResult applicationStartupResult = pythonApplicationRunner_.start(
      createPythonApplication("import iot\n"
                              "display_information = iot.display.information()\n"
                              "assert display_information['connected_display_count'] == 1\n"
                              "assert display_information['connector_name'] == 'HDMI-A-1'\n"
                              "assert display_information['width'] == 1920\n"
                              "assert display_information['height'] == 1080\n"
                              "assert display_information['refresh_rate_hz'] == 60\n"
                              "text_box = iot.display.draw_text_box(10, 20, 300, 80, 'Initial text', "
                              "text_color=(10, 255, 255), "
                              "background_opacity=255, border_width=2, font_size=24)\n"
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

  EXPECT_TRUE(applicationStartupResult.succeeded) << applicationStartupResult.traceback;
}

TEST_F(PythonApplicationRunnerTest, DoesNothingWhenNoApplicationIsRunning) {
  EXPECT_FALSE(pythonApplicationRunner_.isRunning());
  EXPECT_FALSE(pythonApplicationRunner_.timeUntilNextScheduledCallback().has_value());
  EXPECT_TRUE(pythonApplicationRunner_.runScheduledCallbacks().succeeded);
  pythonApplicationRunner_.stop();
}

TEST_F(PythonApplicationRunnerTest, RejectsAZeroByteMicroPythonHeap) {
  EXPECT_THROW(PythonApplicationRunner(screenManager_, tests::testActiveDisplay(), displayManager_,
                                       systemInformationProvider_, 0U),
               std::invalid_argument);
}

TEST_F(PythonApplicationRunnerTest, StopsAfterStartupFailureAndRefusesASecondInterpreter) {
  const PythonExecutionResult failedStartupResult =
      pythonApplicationRunner_.start(createPythonApplication("raise RuntimeError('startup failed')\n"));
  EXPECT_FALSE(failedStartupResult.succeeded);
  EXPECT_FALSE(pythonApplicationRunner_.isRunning());

  ASSERT_TRUE(pythonApplicationRunner_.start(createPythonApplication("value = 1\n")).succeeded);
  EXPECT_TRUE(pythonApplicationRunner_.isRunning());
  EXPECT_THROW(pythonApplicationRunner_.start(createPythonApplication("value = 2\n")), std::logic_error);
  pythonApplicationRunner_.stop();
  EXPECT_FALSE(pythonApplicationRunner_.isRunning());
}

} // namespace
} // namespace python
} // namespace iot
