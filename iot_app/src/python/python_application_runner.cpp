#include "iot/python/python_application_runner.h"

#include "iot/python/micropython_application_context.h"
#include "iot/ui/screen_manager.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace iot {
namespace python {

PythonApplicationRunner::PythonApplicationRunner(ui::ScreenManager &screenManager, display::ActiveDisplay activeDisplay,
                                                 const display::IDisplayManager           &displayManager,
                                                 const system::ISystemInformationProvider &systemInformationProvider,
                                                 std::size_t                               pythonHeapSizeInBytes)
    : screenManager_(screenManager), activeDisplay_(std::move(activeDisplay)), displayManager_(displayManager),
      systemInformationProvider_(systemInformationProvider), pythonHeapSizeInBytes_(pythonHeapSizeInBytes) {
  if (pythonHeapSizeInBytes_ == 0U) {
    IOT_LOG_ERROR(logger_, "Cannot create application runner because pythonHeapSizeInBytes is zero");
    throw std::invalid_argument("PythonApplicationRunner requires a non-empty Python heap");
  }
}

PythonApplicationRunner::~PythonApplicationRunner() {
  stop();
}

PythonExecutionResult PythonApplicationRunner::start(const PythonApplication &pythonApplication) {
  if (isRunning()) {
    IOT_LOG_ERROR(logger_, "Cannot start application '", pythonApplication.applicationName,
                  "' because another MicroPython interpreter is still running");
    throw std::logic_error("Stop the active Python application before starting another one");
  }

  IOT_LOG_DEBUG(logger_, "Starting application id=", pythonApplication.applicationId, ", name='",
                pythonApplication.applicationName, "', entryPoint=", pythonApplication.entryPointPath,
                ", sourceBytes=", pythonApplication.sourceCode.size(), ", heapBytes=", pythonHeapSizeInBytes_);
  screenManager_.throwIfRenderThreadFailed();
  try {
    microPythonApplicationContext_ = std::make_unique<MicroPythonApplicationContext>(
        screenManager_, activeDisplay_, displayManager_.connectedDisplays(), systemInformationProvider_,
        systemInformationProvider_.readSystemInformation(), pythonApplication.applicationName);
    pythonRuntime_ = std::make_unique<MicroPythonRuntime>(pythonHeapSizeInBytes_);

    const auto pythonExecutionResult = pythonRuntime_->executeApplication(pythonApplication);
    if (!pythonExecutionResult.succeeded) {
      stop();
    } else {
      // Start timer intervals after startup finishes. Otherwise a slow startup
      // could make every new timer overdue immediately.
      previousSchedulerUpdateTime_ = std::chrono::steady_clock::now();
    }
    return pythonExecutionResult;
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(logger_, "Application startup threw an exception; id=", pythonApplication.applicationId, ", name='",
                  pythonApplication.applicationName, "', entryPoint=", pythonApplication.entryPointPath, ": ",
                  error.what());
    stop();
    throw;
  } catch (...) {
    IOT_LOG_ERROR(logger_, "Application startup threw an unknown exception; id=", pythonApplication.applicationId,
                  ", name='", pythonApplication.applicationName, "', entryPoint=", pythonApplication.entryPointPath);
    stop();
    throw;
  }
}

void PythonApplicationRunner::stop() noexcept {
  // Destroy MicroPython before the C++ context used by its native modules. The
  // screen manager stays alive because the next app will reuse it.
  pythonRuntime_.reset();
  microPythonApplicationContext_.reset();
  previousSchedulerUpdateTime_.reset();
}

bool PythonApplicationRunner::isRunning() const noexcept {
  return pythonRuntime_ != nullptr;
}

std::optional<std::chrono::milliseconds> PythonApplicationRunner::timeUntilNextScheduledCallback() const {
  if (!pythonRuntime_) {
    return std::nullopt;
  }
  return pythonRuntime_->timeUntilNextScheduledCallback();
}

PythonExecutionResult PythonApplicationRunner::runScheduledCallbacks() {
  if (!pythonRuntime_ || !previousSchedulerUpdateTime_) {
    return {true, {}};
  }

  const auto currentTime = std::chrono::steady_clock::now();
  const auto elapsedTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - *previousSchedulerUpdateTime_);
  // Pass whole milliseconds to MicroPython and keep the smaller remainder for
  // the next call. Without this, frequent MQTT messages would slowly make the
  // Python timers lose time.
  *previousSchedulerUpdateTime_ += elapsedTime;
  return pythonRuntime_->runScheduledCallbacks(elapsedTime);
}

} // namespace python
} // namespace iot
