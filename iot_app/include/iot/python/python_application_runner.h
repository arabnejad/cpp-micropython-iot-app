#pragma once

#include "iot/display/display_manager.h"
#include "iot/logging/logger.h"
#include "iot/python/micropython_runtime.h"
#include "iot/python/python_application.h"
#include "iot/system/system_information.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

namespace iot {
namespace ui {
class ScreenManager;
}

namespace python {

class MicroPythonApplicationContext;

/**
 * Owns the MicroPython interpreter used by the current application.
 *
 * This class starts and stops one clean Python session at a time. It does not
 * decide which app should run or what to do after a failure; that belongs to
 * `PythonApplicationManager`.
 */
class PythonApplicationRunner {
public:
  PythonApplicationRunner(ui::ScreenManager &screenManager, display::ActiveDisplay activeDisplay,
                          const display::IDisplayManager           &displayManager,
                          const system::ISystemInformationProvider &systemInformationProvider,
                          std::size_t                               pythonHeapSizeInBytes);
  ~PythonApplicationRunner();

  // Owns one interpreter and application context; copying and moving are disabled.
  PythonApplicationRunner(const PythonApplicationRunner &)            = delete;
  PythonApplicationRunner &operator=(const PythonApplicationRunner &) = delete;
  PythonApplicationRunner(PythonApplicationRunner &&)                 = delete;
  PythonApplicationRunner &operator=(PythonApplicationRunner &&)      = delete;

  /** Starts a fresh interpreter and runs the application's entry point. */
  PythonExecutionResult start(const PythonApplication &pythonApplication);

  /** Stops the interpreter. Calling this more than once is safe. */
  void stop() noexcept;

  /** Checks whether an interpreter is currently running. */
  bool isRunning() const noexcept;

  /** Gets the time until Python's next callback. */
  std::optional<std::chrono::milliseconds> timeUntilNextScheduledCallback() const;

  /** Runs due callbacks using the time passed since the previous call. */
  PythonExecutionResult runScheduledCallbacks();

private:
  logging::Logger                                      logger_{"PythonApplicationRunner"};
  ui::ScreenManager                                   &screenManager_;
  display::ActiveDisplay                               activeDisplay_;
  const display::IDisplayManager                      &displayManager_;
  const system::ISystemInformationProvider            &systemInformationProvider_;
  std::size_t                                          pythonHeapSizeInBytes_{0};
  std::unique_ptr<MicroPythonApplicationContext>       microPythonApplicationContext_;
  std::unique_ptr<MicroPythonRuntime>                  pythonRuntime_;
  std::optional<std::chrono::steady_clock::time_point> previousSchedulerUpdateTime_;
};

} // namespace python
} // namespace iot
