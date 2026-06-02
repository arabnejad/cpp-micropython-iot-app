#include "iot/python/micropython_runtime.h"

#include <gtest/gtest.h>

#include <string>

namespace iot {
namespace python {
namespace {

PythonApplication createPythonApplication(std::string sourceCode) {
  PythonApplication pythonApplication;
  pythonApplication.applicationId   = "test-application";
  pythonApplication.applicationName = "Test application";
  pythonApplication.entryPointPath  = "main.py";
  pythonApplication.sourceCode      = std::move(sourceCode);
  return pythonApplication;
}

TEST(MicroPythonRuntimeTest, ExecutesSimplePythonSource) {
  MicroPythonRuntime microPythonRuntime(256U * 1024U);

  const PythonExecutionResult applicationExecutionResult =
      microPythonRuntime.executeApplication(createPythonApplication("value = 1 + 2\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded);
  EXPECT_TRUE(applicationExecutionResult.traceback.empty());
}

TEST(MicroPythonRuntimeTest, ReturnsThePythonTracebackWhenStartupFails) {
  MicroPythonRuntime microPythonRuntime(256U * 1024U);

  const PythonExecutionResult applicationExecutionResult =
      microPythonRuntime.executeApplication(createPythonApplication("raise RuntimeError('test failure')\n"));

  EXPECT_FALSE(applicationExecutionResult.succeeded);
  EXPECT_NE(applicationExecutionResult.traceback.find("RuntimeError"), std::string::npos);
}

TEST(MicroPythonRuntimeTest, KeepsTheEndOfAnExceptionThatIsLargerThanTheTracebackBuffer) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const std::string           longMessage(10000U, 'x');
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(
      createPythonApplication("raise RuntimeError('" + longMessage + " tail-marker')\n"));

  EXPECT_FALSE(applicationExecutionResult.succeeded);
  EXPECT_LE(applicationExecutionResult.traceback.size(), 8192U);
  EXPECT_NE(applicationExecutionResult.traceback.find("tail-marker"), std::string::npos);
}

TEST(MicroPythonRuntimeTest, RunsAScheduledCallbackAndReportsItsFailure) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationStartupResult = microPythonRuntime.executeApplication(
      createPythonApplication("import iot\n"
                              "def fail_later():\n"
                              "    raise ValueError('scheduled failure')\n"
                              "iot.scheduler.every(milliseconds=10, callback=fail_later)\n"));
  ASSERT_TRUE(applicationStartupResult.succeeded) << applicationStartupResult.traceback;

  const auto timeUntilScheduledCallback = microPythonRuntime.timeUntilNextScheduledCallback();
  ASSERT_TRUE(timeUntilScheduledCallback.has_value());
  EXPECT_EQ(*timeUntilScheduledCallback, std::chrono::milliseconds(10));

  const PythonExecutionResult scheduledCallbackResult =
      microPythonRuntime.runScheduledCallbacks(std::chrono::milliseconds(10));

  EXPECT_FALSE(scheduledCallbackResult.succeeded);
  EXPECT_NE(scheduledCallbackResult.traceback.find("ValueError"), std::string::npos);
}

TEST(MicroPythonRuntimeTest, TreatsANegativeElapsedTimeAsZeroMilliseconds) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationStartupResult = microPythonRuntime.executeApplication(
      createPythonApplication("import iot\n"
                              "iot.scheduler.every(milliseconds=10, callback=lambda: None)\n"));
  ASSERT_TRUE(applicationStartupResult.succeeded) << applicationStartupResult.traceback;

  const PythonExecutionResult callbackResult = microPythonRuntime.runScheduledCallbacks(std::chrono::milliseconds(-10));

  EXPECT_TRUE(callbackResult.succeeded);
  EXPECT_EQ(microPythonRuntime.timeUntilNextScheduledCallback(), std::chrono::milliseconds(10));
}

TEST(MicroPythonRuntimeTest, LimitsVeryLargeElapsedTimesToTheSchedulerIntegerRange) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationStartupResult = microPythonRuntime.executeApplication(
      createPythonApplication("import iot\n"
                              "iot.scheduler.every(milliseconds=10, callback=lambda: None)\n"));
  ASSERT_TRUE(applicationStartupResult.succeeded) << applicationStartupResult.traceback;

  const PythonExecutionResult callbackResult =
      microPythonRuntime.runScheduledCallbacks(std::chrono::milliseconds::max());

  EXPECT_TRUE(callbackResult.succeeded);
}

TEST(MicroPythonRuntimeTest, RequiresANonZeroHeap) {
  EXPECT_THROW(MicroPythonRuntime(0U), std::invalid_argument);
}

TEST(MicroPythonRuntimeTest, LetsAnApplicationCancelAndClearItsOwnTimers) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationStartupResult = microPythonRuntime.executeApplication(
      createPythonApplication("import iot\n"
                              "first = iot.scheduler.every(milliseconds=20, callback=lambda: None)\n"
                              "second = iot.scheduler.every(milliseconds=5, callback=lambda: None)\n"
                              "assert iot.scheduler.cancel(first) is True\n"
                              "assert iot.scheduler.cancel(first) is False\n"
                              "iot.scheduler.clear()\n"));

  ASSERT_TRUE(applicationStartupResult.succeeded) << applicationStartupResult.traceback;
  EXPECT_FALSE(microPythonRuntime.timeUntilNextScheduledCallback().has_value());
}

TEST(MicroPythonRuntimeTest, RejectsInvalidAndExcessiveTimerRequestsInPython) {
  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(
      createPythonApplication("import iot\n"
                              "try:\n"
                              "    iot.scheduler.every(milliseconds=0, callback=lambda: None)\n"
                              "    assert False\n"
                              "except ValueError:\n"
                              "    pass\n"
                              "try:\n"
                              "    iot.scheduler.every(milliseconds=1, callback=1)\n"
                              "    assert False\n"
                              "except TypeError:\n"
                              "    pass\n"
                              "for index in range(128):\n"
                              "    iot.scheduler.every(milliseconds=1, callback=lambda: None)\n"
                              "try:\n"
                              "    iot.scheduler.every(milliseconds=1, callback=lambda: None)\n"
                              "    assert False\n"
                              "except RuntimeError:\n"
                              "    pass\n"));

  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

} // namespace
} // namespace python
} // namespace iot
