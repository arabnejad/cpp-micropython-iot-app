#pragma once

#include "iot/display/display_types.h"
#include "iot/logging/logger.h"
#include "iot/python/python_application.h"
#include "iot/python/python_application_failure.h"
#include "iot/python/python_application_runner.h"

#include <chrono>
#include <optional>
#include <string>

namespace iot {
namespace ui {
class ScreenManager;
}

namespace python {

/** Current application and runtime display state. */
enum class ApplicationState {
  Stopped,
  DefaultApplication,
  ExternalApplication,
  EmergencyScreen,
};

/** Result of trying to start an application received from outside. */
struct ExternalApplicationActivationResult {
  bool        externalApplicationIsRunning{false};
  std::string failureReason;
};

/** Result of running the Python callbacks that were due. */
struct ScheduledApplicationUpdateResult {
  bool        succeeded{true};
  std::string failedApplicationName;
  std::string failureReason;
};

/**
 * Switches Python applications and shows the emergency screen when one fails.
 *
 * `PythonApplicationRunner` owns the current interpreter. This class decides
 * which app to run, keeps the latest error, and shows the emergency screen
 * when an app fails. `ScreenManager` stays alive during a switch.
 */
class PythonApplicationManager {
public:
  PythonApplicationManager(PythonApplicationRunner &applicationRunner, ui::ScreenManager &screenManager,
                           display::ActiveDisplay activeDisplay);
  ~PythonApplicationManager();

  // Controls one runner and display; copying and moving are disabled.
  PythonApplicationManager(const PythonApplicationManager &)            = delete;
  PythonApplicationManager &operator=(const PythonApplicationManager &) = delete;
  PythonApplicationManager(PythonApplicationManager &&)                 = delete;
  PythonApplicationManager &operator=(PythonApplicationManager &&)      = delete;

  /** Starts the default app shipped with the executable. */
  void startDefaultApplication(const PythonApplication &defaultApplication);

  /** Stops the current app and tries to start an app received from outside. */
  ExternalApplicationActivationResult activateExternalApplication(const PythonApplication &pythonApplication);

  /** Stops the current Python app. Calling this more than once is safe. */
  void stop() noexcept;

  /** Gets what is currently running. */
  ApplicationState state() const noexcept;

  /** Gets the name of the current app or emergency screen. */
  const std::string &activeScreenName() const noexcept;

  /** Gets the time until the current app's next callback. */
  std::optional<std::chrono::milliseconds> timeUntilNextScheduledCallback() const;

  /** Runs due callbacks and shows the emergency screen if a callback fails. */
  ScheduledApplicationUpdateResult runScheduledCallbacks();

private:
  void prepareForApplicationStart();
  void recordApplicationFailure(std::string applicationName, std::string phase, std::string traceback);
  bool showEmergencyFailureScreen(std::string explanation) noexcept;

  logging::Logger                         logger_{"PythonApplicationManager"};
  PythonApplicationRunner                &applicationRunner_;
  ui::ScreenManager                      &screenManager_;
  display::ActiveDisplay                  activeDisplay_;
  ApplicationState                        state_{ApplicationState::Stopped};
  std::string                             activeScreenName_;
  std::optional<PythonApplicationFailure> caughtPythonApplicationError_;
};

} // namespace python
} // namespace iot
