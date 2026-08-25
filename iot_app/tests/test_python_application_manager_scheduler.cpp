#include "python_application_manager_test_fixture.h"

#include <chrono>
#include <thread>

namespace iot {
namespace python {
namespace {

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

} // namespace
} // namespace python
} // namespace iot
