#include "iot/python/python_application_manager.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

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
      : recordingRenderBackend_(std::make_unique<tests::RecordingRenderBackend>()),
        recordingRenderBackendView_(recordingRenderBackend_.get()),
        screenManager_(tests::testActiveDisplay(), std::move(recordingRenderBackend_), 16U),
        pythonApplicationRunner_(screenManager_, tests::testActiveDisplay(), displayManager_,
                                 systemInformationProvider_, 256U * 1024U) {}

  void SetUp() override {
    screenManager_.start();
  }

  void TearDown() override {
    pythonApplicationRunner_.stop();
    screenManager_.stop();
  }

  PythonApplicationManager createApplicationManager() {
    return PythonApplicationManager(pythonApplicationRunner_, screenManager_, tests::testActiveDisplay());
  }

  bool waitForEmergencyScreen() {
    return tests::waitUntil([this] {
      std::lock_guard<std::mutex> lock(recordingRenderBackendView_->renderStateMutex);
      return !recordingRenderBackendView_->lastErrorScreenText.empty();
    });
  }

  std::string emergencyScreenText() {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView_->renderStateMutex);
    return recordingRenderBackendView_->lastErrorScreenText;
  }

  std::unique_ptr<tests::RecordingRenderBackend> recordingRenderBackend_;
  tests::RecordingRenderBackend                 *recordingRenderBackendView_;
  ui::ScreenManager                              screenManager_;
  tests::TestDisplayManager                      displayManager_;
  tests::TestSystemInformationProvider           systemInformationProvider_;
  PythonApplicationRunner                        pythonApplicationRunner_;
};

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenTheDefaultApplicationFailsToStart) {
  auto                    pythonApplicationManager = createApplicationManager();
  const PythonApplication brokenDefaultApplication =
      createPythonApplication("Broken default", "raise RuntimeError('broken default')\n");

  pythonApplicationManager.startDefaultApplication(brokenDefaultApplication);

  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("Broken default"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, KeepsTheNewestPartOfALongFailureTracebackOnTheEmergencyScreen) {
  const std::string       longErrorMessage(3000U, 'x');
  auto                    pythonApplicationManager = createApplicationManager();
  const PythonApplication brokenDefaultApplication =
      createPythonApplication("Long failure", "raise RuntimeError('" + longErrorMessage + " newest-part')\n");

  pythonApplicationManager.startDefaultApplication(brokenDefaultApplication);

  ASSERT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("[Earlier traceback text omitted]"), std::string::npos);
  EXPECT_NE(emergencyScreenText().find("newest-part"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, MarksAnExternalApplicationAsRunningAfterSuccessfulStartup) {
  auto pythonApplicationManager = createApplicationManager();

  const ExternalApplicationActivationResult activationResult =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("External", "value = 2\n"));

  EXPECT_TRUE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::ExternalApplication);
  EXPECT_EQ(pythonApplicationManager.activeScreenName(), "External");
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenAnExternalTimerCallbackFails) {
  auto       pythonApplicationManager = createApplicationManager();
  const auto activationResult         = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("External with timer", "import iot\n"
                                                                     "def fail_later():\n"
                                                                     "    raise RuntimeError('timer failed')\n"
                                                                     "iot.scheduler.every(milliseconds=1, callback=fail_later)\n"));
  ASSERT_TRUE(activationResult.externalApplicationIsRunning);

  // Let the one-millisecond timer become due before running scheduled callbacks.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const ScheduledApplicationUpdateResult callbackResult = pythonApplicationManager.runScheduledCallbacks();

  EXPECT_FALSE(callbackResult.succeeded);
  EXPECT_EQ(callbackResult.failedApplicationName, "External with timer");
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("timer failed"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, StartsStopsAndRestartsTheShippedDefaultApplication) {
  auto                    pythonApplicationManager = createApplicationManager();
  const PythonApplication defaultApplication       = createPythonApplication("Default", "value = 1\n");

  EXPECT_TRUE(pythonApplicationManager.runScheduledCallbacks().succeeded);
  EXPECT_FALSE(pythonApplicationManager.timeUntilNextScheduledCallback().has_value());

  pythonApplicationManager.startDefaultApplication(defaultApplication);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::DefaultApplication);
  EXPECT_EQ(pythonApplicationManager.activeScreenName(), "Default");

  pythonApplicationManager.stop();
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::Stopped);
  EXPECT_TRUE(pythonApplicationManager.activeScreenName().empty());

  pythonApplicationManager.startDefaultApplication(defaultApplication);
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::DefaultApplication);
}

TEST_F(PythonApplicationManagerTest, ShowsTheEmergencyScreenWhenAnExternalApplicationFailsToStart) {
  auto pythonApplicationManager = createApplicationManager();

  const ExternalApplicationActivationResult activationResult = pythonApplicationManager.activateExternalApplication(
      createPythonApplication("Broken external", "raise RuntimeError('external broke')\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(activationResult.failureReason, "Python raised an exception while starting the external application");
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("external broke"), std::string::npos);
}

TEST_F(PythonApplicationManagerTest, RejectsAnEmptyDefaultApplication) {
  auto pythonApplicationManager = createApplicationManager();

  EXPECT_THROW(pythonApplicationManager.startDefaultApplication(PythonApplication{}), std::invalid_argument);
}

TEST_F(PythonApplicationManagerTest, ShowsAnEmergencyScreenWhenReadingSystemInformationThrowsAnException) {
  ThrowingSystemInformationProvider throwingSystemInformationProvider;
  PythonApplicationRunner           runnerWithThrowingSystemInformationProvider(
      screenManager_, tests::testActiveDisplay(), displayManager_, throwingSystemInformationProvider, 256U * 1024U);
  PythonApplicationManager pythonApplicationManager(runnerWithThrowingSystemInformationProvider, screenManager_,
                                                    tests::testActiveDisplay());

  const ExternalApplicationActivationResult activationResult =
      pythonApplicationManager.activateExternalApplication(createPythonApplication("External", "value = 2\n"));

  EXPECT_FALSE(activationResult.externalApplicationIsRunning);
  EXPECT_EQ(activationResult.failureReason, "system snapshot failed");
  EXPECT_EQ(pythonApplicationManager.state(), ApplicationState::EmergencyScreen);
  ASSERT_TRUE(waitForEmergencyScreen());
  EXPECT_NE(emergencyScreenText().find("system snapshot failed"), std::string::npos);
}

} // namespace
} // namespace python
} // namespace iot
