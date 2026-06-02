#include "iot/python/python_application_manager.h"

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

PythonApplicationManager::PythonApplicationManager(PythonApplicationRunner &applicationRunner,
                                                   ui::ScreenManager       &screenManager,
                                                   display::ActiveDisplay   activeDisplay)
    : applicationRunner_(applicationRunner), screenManager_(screenManager), activeDisplay_(std::move(activeDisplay)) {}

PythonApplicationManager::~PythonApplicationManager() {
  stop();
}

void PythonApplicationManager::startDefaultApplication(const PythonApplication &defaultApplication) {
  if (defaultApplication.applicationId.empty() || defaultApplication.sourceCode.empty()) {
    IOT_LOG_ERROR(logger_, "Cannot start default application; applicationId='", defaultApplication.applicationId,
                  "', sourceBytes=", defaultApplication.sourceCode.size());
    throw std::invalid_argument("Python application manager requires a valid default application");
  }
  prepareForApplicationStart();

  const auto pythonExecutionResult = applicationRunner_.start(defaultApplication);
  if (pythonExecutionResult.succeeded) {
    state_            = ApplicationState::DefaultApplication;
    activeScreenName_ = defaultApplication.applicationName;
    return;
  }

  recordApplicationFailure(defaultApplication.applicationName, "Startup", pythonExecutionResult.traceback);
  if (!showEmergencyFailureScreen("The shipped default Python application failed, so no Python app is running.")) {
    IOT_LOG_ERROR(logger_, "Default application and emergency screen both failed; applicationId=",
                  defaultApplication.applicationId, ", display=", activeDisplay_.mode().width, 'x',
                  activeDisplay_.mode().height);
    throw std::runtime_error("The shipped default Python application failed and its emergency screen could not start");
  }
}

ExternalApplicationActivationResult
PythonApplicationManager::activateExternalApplication(const PythonApplication &pythonApplication) {
  ExternalApplicationActivationResult activationResult;
  prepareForApplicationStart();

  try {
    const auto pythonExecutionResult = applicationRunner_.start(pythonApplication);
    if (pythonExecutionResult.succeeded) {
      caughtPythonApplicationError_.reset();
      state_                                        = ApplicationState::ExternalApplication;
      activeScreenName_                             = pythonApplication.applicationName;
      activationResult.externalApplicationIsRunning = true;
      return activationResult;
    }
    activationResult.failureReason = "Python raised an exception while starting the external application";
    recordApplicationFailure(pythonApplication.applicationName, "Startup", pythonExecutionResult.traceback);
  } catch (const std::exception &error) {
    activationResult.failureReason = error.what();
    recordApplicationFailure(pythonApplication.applicationName, "Startup", error.what());
  } catch (...) {
    activationResult.failureReason = "Unknown failure while starting the external application";
    recordApplicationFailure(pythonApplication.applicationName, "Startup", activationResult.failureReason);
  }

  applicationRunner_.stop();
  if (!showEmergencyFailureScreen("The external Python application failed to start, so no Python app is running.")) {
    activationResult.failureReason += "; the runtime could not show the emergency screen";
  }
  return activationResult;
}

void PythonApplicationManager::prepareForApplicationStart() {
  applicationRunner_.stop();
  state_ = ApplicationState::Stopped;
  activeScreenName_.clear();
  screenManager_.clear({8U, 13U, 22U});
}

void PythonApplicationManager::stop() noexcept {
  applicationRunner_.stop();
  state_ = ApplicationState::Stopped;
  activeScreenName_.clear();
}

ApplicationState PythonApplicationManager::state() const noexcept {
  return state_;
}

const std::string &PythonApplicationManager::activeScreenName() const noexcept {
  return activeScreenName_;
}

std::optional<std::chrono::milliseconds> PythonApplicationManager::timeUntilNextScheduledCallback() const {
  return applicationRunner_.timeUntilNextScheduledCallback();
}

ScheduledApplicationUpdateResult PythonApplicationManager::runScheduledCallbacks() {
  ScheduledApplicationUpdateResult scheduledUpdateResult;
  const auto                       callbackExecution = applicationRunner_.runScheduledCallbacks();
  if (callbackExecution.succeeded) {
    return scheduledUpdateResult;
  }

  scheduledUpdateResult.succeeded             = false;
  scheduledUpdateResult.failedApplicationName = activeScreenName_;
  scheduledUpdateResult.failureReason         = "A scheduled Python callback raised an exception";
  recordApplicationFailure(scheduledUpdateResult.failedApplicationName, "Scheduled callback",
                           callbackExecution.traceback);
  applicationRunner_.stop();

  if (!showEmergencyFailureScreen(
          "The Python application stopped after a scheduled callback failed. No Python app is running.")) {
    scheduledUpdateResult.failureReason += "; the runtime could not show the emergency screen";
  }
  return scheduledUpdateResult;
}

void PythonApplicationManager::recordApplicationFailure(std::string applicationName, std::string phase,
                                                        std::string traceback) {
  if (traceback.empty()) {
    traceback = "The application failed without a Python traceback.";
  }
  IOT_LOG_ERROR(logger_, "Recorded Python application failure; name='", applicationName, "', phase=", phase,
                ", reason='", tracebackSummaryForLog(traceback), "', tracebackBytes=", traceback.size());
  caughtPythonApplicationError_ = PythonApplicationFailure{std::move(applicationName), std::move(phase),
                                                           currentLocalTimestamp(), std::move(traceback)};
}

bool PythonApplicationManager::showEmergencyFailureScreen(std::string explanation) noexcept {
  if (!caughtPythonApplicationError_) {
    return false;
  }

  applicationRunner_.stop();
  state_            = ApplicationState::EmergencyScreen;
  activeScreenName_ = "Emergency screen";

  try {
    const auto width  = activeDisplay_.mode().width;
    const auto height = activeDisplay_.mode().height;
    const auto margin = static_cast<std::int32_t>(std::max<std::uint32_t>(20U, width / 16U));

    ui::TextBoxSpec failureBox;
    failureBox.bounds = {margin, margin, static_cast<std::int32_t>(width) - (2 * margin),
                         static_cast<std::int32_t>(height) - (2 * margin)};
    failureBox.text   = "IoT App - Emergency Screen\n\n" + std::move(explanation) +
                      "\n\nApplication: " + caughtPythonApplicationError_->applicationName +
                      "\nPhase: " + caughtPythonApplicationError_->phase +
                      "\nTime: " + caughtPythonApplicationError_->timestamp + "\n\n" +
                      shortenTracebackForScreen(caughtPythonApplicationError_->traceback);
    failureBox.textColor         = {254U, 226U, 226U};
    failureBox.backgroundColor   = {69U, 10U, 10U};
    failureBox.borderColor       = {248U, 113U, 113U};
    failureBox.backgroundOpacity = 255U;
    failureBox.borderWidth       = 2U;
    failureBox.fontSize          = width >= 1600U ? 24U : 18U;
    screenManager_.showErrorScreen(failureBox);
    return true;
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(logger_, "Could not show application failure on screen: ", error.what(),
                  "; display=", activeDisplay_.mode().width, 'x', activeDisplay_.mode().height);
  } catch (...) {
    IOT_LOG_ERROR(logger_, "Could not show application failure on screen because of an unknown exception; display=",
                  activeDisplay_.mode().width, 'x', activeDisplay_.mode().height);
  }

  state_ = ApplicationState::Stopped;
  activeScreenName_.clear();
  return false;
}

} // namespace python
} // namespace iot
