#include "iot/python/python_application_manager.h"

#include "iot/python/micropython_application_context.h"
#include "iot/ui/screen_manager.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <utility>

namespace iot {
namespace python {
namespace {

struct PythonApplicationFailure {
  std::string applicationName;
  std::string failurePhase;
  std::string timestamp;
  std::string traceback;
};

std::string currentLocalTimestamp() {
  const std::time_t currentTime = std::time(nullptr);
  std::tm           localTime{};
  if (currentTime == static_cast<std::time_t>(-1) || localtime_r(&currentTime, &localTime) == nullptr) {
    return "Time unavailable";
  }

  std::array<char, 20U> timestamp{};
  if (std::strftime(timestamp.data(), timestamp.size(), "%Y-%m-%d %H:%M:%S", &localTime) == 0U) {
    return "Time unavailable";
  }
  return timestamp.data();
}

std::string shortenTracebackForScreen(const std::string &traceback) {
  constexpr std::size_t maximumVisibleCharacters = 2500U;
  if (traceback.size() <= maximumVisibleCharacters) {
    return traceback;
  }
  return "[Earlier traceback text omitted]\n" + traceback.substr(traceback.size() - maximumVisibleCharacters);
}

std::string tracebackSummaryForLog(const std::string &traceback) {
  constexpr std::size_t maximumSummarySize = 240U;
  const auto            lastLineEnd        = traceback.find_last_not_of("\r\n");
  if (lastLineEnd == std::string::npos) {
    return "No Python traceback was provided";
  }

  const auto  lastLineStart = traceback.find_last_of("\r\n", lastLineEnd);
  const auto  summaryStart  = lastLineStart == std::string::npos ? 0U : lastLineStart + 1U;
  std::string summary       = traceback.substr(summaryStart, lastLineEnd - summaryStart + 1U);
  if (summary.size() > maximumSummarySize) {
    summary.resize(maximumSummarySize);
    summary += "...";
  }
  return summary;
}

} // namespace

PythonApplicationManager::PythonApplicationManager(ui::ScreenManager                        &screenManager,
                                                   display::ActiveDisplay                    activeDisplay,
                                                   std::vector<display::DisplayInfo>         connectedDisplays,
                                                   const system::ISystemInformationProvider &systemInformationProvider,
                                                   std::size_t                               pythonHeapSizeInBytes)
    : m_screenManager(screenManager), m_activeDisplay(std::move(activeDisplay)),
      m_connectedDisplays(std::move(connectedDisplays)), m_systemInformationProvider(systemInformationProvider),
      m_pythonHeapSizeInBytes(pythonHeapSizeInBytes) {
  if (m_pythonHeapSizeInBytes == 0U) {
    IOT_LOG_ERROR(m_logger, "Cannot create the Python application manager because pythonHeapSizeInBytes is zero");
    throw std::invalid_argument("PythonApplicationManager requires a non-empty Python heap");
  }
}

PythonApplicationManager::~PythonApplicationManager() {
  stop();
}

void PythonApplicationManager::startDefaultApplication(const PythonApplication &defaultApplication) {
  if (defaultApplication.applicationId.empty() || defaultApplication.sourceCode.empty()) {
    IOT_LOG_ERROR(m_logger, "Cannot start default application; applicationId='", defaultApplication.applicationId,
                  "', sourceBytes=", defaultApplication.sourceCode.size());
    throw std::invalid_argument("Python application manager requires a valid default application");
  }

  prepareForApplicationStart();
  std::string failureTraceback;
  try {
    const auto pythonExecutionResult = startApplicationInNewInterpreter(defaultApplication);
    if (pythonExecutionResult.succeeded) {
      m_state            = ApplicationState::DefaultApplication;
      m_activeScreenName = defaultApplication.applicationName;
      return;
    }
    failureTraceback = pythonExecutionResult.traceback;
  } catch (const std::exception &error) {
    failureTraceback = error.what();
  } catch (...) {
    failureTraceback = "Unknown failure while starting the shipped default application";
  }

  if (!showEmergencyScreenForApplicationFailure(
          "The shipped default Python application failed, so no Python app is running.",
          defaultApplication.applicationName, "Startup", std::move(failureTraceback))) {
    IOT_LOG_ERROR(m_logger, "Default application and emergency screen both failed; applicationId=",
                  defaultApplication.applicationId, ", display=", m_activeDisplay.mode().width, 'x',
                  m_activeDisplay.mode().height);
    throw std::runtime_error("The shipped default Python application failed and its emergency screen could not start");
  }
}

ExternalApplicationActivationResult
PythonApplicationManager::activateExternalApplication(const PythonApplication &pythonApplication) {
  ExternalApplicationActivationResult activationResult;
  prepareForApplicationStart();

  std::string failureTraceback;
  try {
    const auto pythonExecutionResult = startApplicationInNewInterpreter(pythonApplication);
    if (pythonExecutionResult.succeeded) {
      m_state                                       = ApplicationState::ExternalApplication;
      m_activeScreenName                            = pythonApplication.applicationName;
      activationResult.externalApplicationIsRunning = true;
      return activationResult;
    }
    activationResult.failureReason = "Python raised an exception while starting the external application";
    failureTraceback               = pythonExecutionResult.traceback;
  } catch (const std::exception &error) {
    activationResult.failureReason = error.what();
    failureTraceback               = error.what();
  } catch (...) {
    activationResult.failureReason = "Unknown failure while starting the external application";
    failureTraceback               = activationResult.failureReason;
  }

  stopPythonInterpreter();
  if (!showEmergencyScreenForApplicationFailure(
          "The external Python application failed to start, so no Python app is running.",
          pythonApplication.applicationName, "Startup", std::move(failureTraceback))) {
    activationResult.failureReason += "; the runtime could not show the emergency screen";
  }
  return activationResult;
}

PythonExecutionResult
PythonApplicationManager::startApplicationInNewInterpreter(const PythonApplication &pythonApplication) {
  if (m_microPythonRuntime != nullptr) {
    IOT_LOG_ERROR(m_logger, "Cannot start application '", pythonApplication.applicationName,
                  "' because another MicroPython interpreter is still running");
    throw std::logic_error("Stop the active Python application before starting another one");
  }

  IOT_LOG_DEBUG(m_logger, "Starting application id=", pythonApplication.applicationId, ", name='",
                pythonApplication.applicationName, "', entryPoint=", pythonApplication.entryPointPath,
                ", sourceBytes=", pythonApplication.sourceCode.size(), ", heapBytes=", m_pythonHeapSizeInBytes);
  m_screenManager.throwIfRenderThreadFailed();
  try {
    m_microPythonApplicationContext = std::make_unique<MicroPythonApplicationContext>(
        m_screenManager, m_activeDisplay, m_connectedDisplays, m_systemInformationProvider,
        m_systemInformationProvider.readSystemInformation(), pythonApplication.applicationName);
    m_microPythonRuntime = std::make_unique<MicroPythonRuntime>(m_pythonHeapSizeInBytes);

    const auto pythonExecutionResult = m_microPythonRuntime->executeApplication(pythonApplication);
    if (!pythonExecutionResult.succeeded) {
      stopPythonInterpreter();
    } else {
      // Timer intervals start after main.py has finished. A slow startup must
      // not make every newly created timer overdue.
      m_previousSchedulerUpdateTime = std::chrono::steady_clock::now();
    }
    return pythonExecutionResult;
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(m_logger, "Application startup threw an exception; id=", pythonApplication.applicationId, ", name='",
                  pythonApplication.applicationName, "', entryPoint=", pythonApplication.entryPointPath, ": ",
                  error.what());
    stopPythonInterpreter();
    throw;
  } catch (...) {
    IOT_LOG_ERROR(m_logger, "Application startup threw an unknown exception; id=", pythonApplication.applicationId,
                  ", name='", pythonApplication.applicationName, "', entryPoint=", pythonApplication.entryPointPath);
    stopPythonInterpreter();
    throw;
  }
}

void PythonApplicationManager::prepareForApplicationStart() {
  stopPythonInterpreter();
  m_state = ApplicationState::Stopped;
  m_activeScreenName.clear();
  m_screenManager.clear({8U, 13U, 22U});
}

void PythonApplicationManager::stopPythonInterpreter() noexcept {
  // MicroPython must be destroyed before the context used by its native
  // modules. ScreenManager remains alive for the next application.
  m_microPythonRuntime.reset();
  m_microPythonApplicationContext.reset();
  m_previousSchedulerUpdateTime.reset();
}

void PythonApplicationManager::stop() noexcept {
  stopPythonInterpreter();
  m_state = ApplicationState::Stopped;
  m_activeScreenName.clear();
}

ApplicationState PythonApplicationManager::state() const noexcept {
  return m_state;
}

const std::string &PythonApplicationManager::activeScreenName() const noexcept {
  return m_activeScreenName;
}

std::optional<std::chrono::milliseconds> PythonApplicationManager::timeUntilNextScheduledCallback() const {
  if (m_microPythonRuntime == nullptr) {
    return std::nullopt;
  }
  return m_microPythonRuntime->timeUntilNextScheduledCallback();
}

void PythonApplicationManager::runScheduledCallbacks() {
  if (m_microPythonRuntime == nullptr || !m_previousSchedulerUpdateTime) {
    return;
  }

  const auto currentTime = std::chrono::steady_clock::now();
  const auto elapsedTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - *m_previousSchedulerUpdateTime);
  // Keep fractions of a millisecond for the next update. Otherwise frequent
  // MQTT messages could slowly make Python timers lose time.
  *m_previousSchedulerUpdateTime += elapsedTime;

  const auto callbackExecutionResult = m_microPythonRuntime->runScheduledCallbacks(elapsedTime);
  if (callbackExecutionResult.succeeded) {
    return;
  }

  const std::string failedApplicationName = m_activeScreenName;
  stopPythonInterpreter();
  showEmergencyScreenForApplicationFailure(
      "The Python application stopped after a scheduled callback failed. No Python app is running.",
      failedApplicationName, "Scheduled callback", callbackExecutionResult.traceback);
}

bool PythonApplicationManager::showEmergencyScreenForApplicationFailure(std::string explanation,
                                                                        std::string applicationName,
                                                                        std::string failurePhase,
                                                                        std::string traceback) noexcept {
  if (traceback.empty()) {
    traceback = "The application failed without a Python traceback.";
  }

  IOT_LOG_ERROR(m_logger, "Python application failed; name='", applicationName, "', phase=", failurePhase, ", reason='",
                tracebackSummaryForLog(traceback), "', tracebackBytes=", traceback.size());
  const PythonApplicationFailure applicationFailure{std::move(applicationName), std::move(failurePhase),
                                                    currentLocalTimestamp(), std::move(traceback)};

  stopPythonInterpreter();
  m_state            = ApplicationState::EmergencyScreen;
  m_activeScreenName = "Emergency screen";

  try {
    const auto width  = m_activeDisplay.mode().width;
    const auto height = m_activeDisplay.mode().height;
    const auto margin = static_cast<std::int32_t>(std::max<std::uint32_t>(20U, width / 16U));

    ui::TextBoxSpec failureTextBox;
    failureTextBox.bounds = {margin, margin, static_cast<std::int32_t>(width) - (2 * margin),
                             static_cast<std::int32_t>(height) - (2 * margin)};
    failureTextBox.text   = "IoT App - Emergency Screen\n\n" + std::move(explanation) +
                          "\n\nApplication: " + applicationFailure.applicationName +
                          "\nPhase: " + applicationFailure.failurePhase + "\nTime: " + applicationFailure.timestamp +
                          "\n\n" + shortenTracebackForScreen(applicationFailure.traceback);
    failureTextBox.textColor         = {254U, 226U, 226U};
    failureTextBox.backgroundColor   = {69U, 10U, 10U};
    failureTextBox.borderColor       = {248U, 113U, 113U};
    failureTextBox.backgroundOpacity = 255U;
    failureTextBox.borderWidth       = 2U;
    failureTextBox.fontSize          = width >= 1600U ? 24U : 18U;
    m_screenManager.showErrorScreen(failureTextBox);
    return true;
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(m_logger, "Could not show application failure on screen: ", error.what(),
                  "; display=", m_activeDisplay.mode().width, 'x', m_activeDisplay.mode().height);
  } catch (...) {
    IOT_LOG_ERROR(m_logger, "Could not show application failure on screen because of an unknown exception; display=",
                  m_activeDisplay.mode().width, 'x', m_activeDisplay.mode().height);
  }

  m_state = ApplicationState::Stopped;
  m_activeScreenName.clear();
  return false;
}

} // namespace python
} // namespace iot
