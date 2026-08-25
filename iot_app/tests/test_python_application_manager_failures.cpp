#include "python_application_manager_test_fixture.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace iot {
namespace python {
namespace {

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

} // namespace
} // namespace python
} // namespace iot
