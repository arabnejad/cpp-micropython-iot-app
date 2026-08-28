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
#include <string>

namespace iot {
namespace ui {
class ScreenManager;
}

namespace python {

class MicroPythonApplicationContext;

/* Current application and runtime display state.
 * What the runtime is currently showing.
 */
enum class ApplicationState {
  Stopped,
  DefaultApplication,
  ExternalApplication,
  EmergencyScreen,
};

/* Result of trying to start an application received from outside. */
struct ExternalApplicationActivationResult {
  bool        externalApplicationIsRunning{false};
  std::string failureReason;
};

/*
 * Controls the lifetime of the current Python application.
 *
 * Each application gets a fresh MicroPython interpreter and context. The
 * manager also advances timers, stops the old interpreter during a switch, and
 * shows the emergency screen after an unhandled Python exception.
 */
class PythonApplicationManager {
public:
  PythonApplicationManager(ui::ScreenManager &screenManager, display::ActiveDisplay activeDisplay,
                           const display::IDisplayManager           &displayManager,
                           const system::ISystemInformationProvider &systemInformationProvider,
                           std::size_t                               pythonHeapSizeInBytes);
  ~PythonApplicationManager();

  // Owns one interpreter and its application context. Copying and moving are
  // disabled so that ownership cannot be split between manager objects.
  PythonApplicationManager(const PythonApplicationManager &)            = delete;
  PythonApplicationManager &operator=(const PythonApplicationManager &) = delete;
  PythonApplicationManager(PythonApplicationManager &&)                 = delete;
  PythonApplicationManager &operator=(PythonApplicationManager &&)      = delete;

  /* Starts the default app shipped with IoT App. */
  void startDefaultApplication(const PythonApplication &defaultApplication);

  /* Stops the current app and tries to start an app received from outside. */
  ExternalApplicationActivationResult activateExternalApplication(const PythonApplication &pythonApplication);

  /* Stops the current Python app. */
  void stop() noexcept;

  /* Gets the current runtime state. */
  ApplicationState state() const noexcept;

  /* Gets the name of the active app, or "Emergency screen" after a failure. */
  const std::string &activeScreenName() const noexcept;

  /* Gets the time remaining before the current app's next callback. */
  std::optional<std::chrono::milliseconds> timeUntilNextScheduledCallback() const;

  /* Runs callbacks that are due and shows the emergency screen if one fails. */
  void runScheduledCallbacks();

private:
  PythonExecutionResult startApplicationInNewInterpreter(const PythonApplication &pythonApplication);
  void                  stopPythonInterpreter() noexcept;
  void                  prepareForApplicationStart();
  bool                  showEmergencyScreenForApplicationFailure(std::string explanation, std::string applicationName,
                                                                 std::string failurePhase, std::string traceback) noexcept;

  logging::Logger                                      m_logger{"PythonApplicationManager"};
  ui::ScreenManager                                   &m_screenManager;
  display::ActiveDisplay                               m_activeDisplay;
  const display::IDisplayManager                      &m_displayManager;
  const system::ISystemInformationProvider            &m_systemInformationProvider;
  std::size_t                                          m_pythonHeapSizeInBytes{0};
  std::unique_ptr<MicroPythonApplicationContext>       m_microPythonApplicationContext;
  std::unique_ptr<MicroPythonRuntime>                  m_microPythonRuntime;
  std::optional<std::chrono::steady_clock::time_point> m_previousSchedulerUpdateTime;
  ApplicationState                                     m_state{ApplicationState::Stopped};
  std::string                                          m_activeScreenName;
};

} // namespace python
} // namespace iot
