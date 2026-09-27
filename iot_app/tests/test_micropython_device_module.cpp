#include "iot/python/micropython_runtime.h"

#include <gtest/gtest.h>

namespace iot {
namespace python {
namespace {

TEST(MicroPythonDeviceModuleTest, PublicIotModuleExposesEveryNativeSubsystem) {
  PythonApplication pythonApplication;
  pythonApplication.applicationId   = "device-module-test";
  pythonApplication.applicationName = "Device module test";
  pythonApplication.entryPointPath  = "main.py";
  pythonApplication.sourceCode      = "import iot\n"
                                      "assert iot.display is not None\n"
                                      "assert iot.input is not None\n"
                                      "assert iot.network is not None\n"
                                      "assert iot.scheduler is not None\n"
                                      "assert iot.system is not None\n";

  MicroPythonRuntime          microPythonRuntime(256U * 1024U);
  const PythonExecutionResult applicationExecutionResult = microPythonRuntime.executeApplication(pythonApplication);

  // The << applicationExecutionResult.traceback part adds the Python traceback to
  // the test failure message only if the check fails. It does not print
  // the traceback when the test passes or change the result being checked.
  EXPECT_TRUE(applicationExecutionResult.succeeded) << applicationExecutionResult.traceback;
}

} // namespace
} // namespace python
} // namespace iot
