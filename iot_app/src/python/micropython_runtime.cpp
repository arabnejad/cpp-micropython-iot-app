#include "iot/python/micropython_runtime.h"

#include "micropython_execution.h"
#include "iot_scheduler_runtime.h"

extern "C" {
#include "port/micropython_embed.h"
}

#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace iot {
namespace python {

namespace {

constexpr std::size_t maximumCapturedTracebackSizeInBytes = 8U * 1024U;

std::size_t numberOfHeapWords(std::size_t heapSizeInBytes) {
  if (heapSizeInBytes == 0U) {
    throw std::invalid_argument("MicroPython heap size must be greater than zero");
  }

  const std::size_t wordSize = sizeof(std::uintptr_t);
  return (heapSizeInBytes + wordSize - 1U) / wordSize;
}

} // namespace

MicroPythonRuntime::MicroPythonRuntime(std::size_t heapSizeInBytes)
    : m_heapWords(numberOfHeapWords(heapSizeInBytes)), m_ownerThreadId(std::this_thread::get_id()) {
  IOT_LOG_DEBUG(m_logger, "Starting MicroPython interpreter; requestedHeapBytes=", heapSizeInBytes,
                ", allocatedHeapBytes=", m_heapWords.size() * sizeof(std::uintptr_t));
  // MicroPython needs an address near the top of this thread's stack so the
  // garbage collector knows which area to scan.
  std::uintptr_t stackTopMarker = 0U;
  mp_embed_init(m_heapWords.data(), m_heapWords.size() * sizeof(std::uintptr_t), &stackTopMarker);
  // The embed port reuses some global VM state. Clear old timer references
  // before the new interpreter starts using its own heap.
  iot_scheduler_reset();
}

MicroPythonRuntime::~MicroPythonRuntime() {
  IOT_LOG_DEBUG(m_logger, "Stopping MicroPython interpreter");
  mp_embed_deinit();
}

PythonExecutionResult MicroPythonRuntime::executeApplication(const PythonApplication &pythonApplication) {
  throwIfCalledFromAnotherThread();
  if (pythonApplication.applicationName.empty() || pythonApplication.entryPointPath.empty() ||
      pythonApplication.sourceCode.empty()) {
    throw std::invalid_argument("Python application is empty");
  }

  m_previousSchedulerUpdateTime.reset();
  const std::string entryPointName = pythonApplication.entryPointPath.string();
  IOT_LOG_INFO(m_logger, "Python app '", pythonApplication.applicationName, "' is starting ", entryPointName);
  std::array<char, maximumCapturedTracebackSizeInBytes + 1U> tracebackBuffer{};
  const int succeeded = iot_micropython_execute_source(entryPointName.c_str(), pythonApplication.sourceCode.data(),
                                                       pythonApplication.sourceCode.size(), tracebackBuffer.data(),
                                                       tracebackBuffer.size());
  if (succeeded != 0) {
    // Timer intervals start after main.py has finished. Time used by startup
    // should not make newly created timers overdue.
    m_previousSchedulerUpdateTime = std::chrono::steady_clock::now();
    IOT_LOG_INFO(m_logger, "Python app '", pythonApplication.applicationName, "' started successfully");
  } else {
    IOT_LOG_ERROR(m_logger, "Python app '", pythonApplication.applicationName, "' failed during startup");
  }
  return {succeeded != 0, tracebackBuffer.data()};
}

std::optional<std::chrono::milliseconds> MicroPythonRuntime::timeUntilNextScheduledCallback() const {
  throwIfCalledFromAnotherThread();
  std::uint32_t delayMilliseconds = 0U;
  if (iot_scheduler_next_delay_milliseconds(&delayMilliseconds) == 0) {
    return std::nullopt;
  }
  const auto scheduledDelay = std::chrono::milliseconds(delayMilliseconds);
  if (!m_previousSchedulerUpdateTime) {
    return scheduledDelay;
  }

  const auto elapsedTimeSinceSchedulerUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - *m_previousSchedulerUpdateTime);
  if (elapsedTimeSinceSchedulerUpdate <= std::chrono::milliseconds::zero()) {
    return scheduledDelay;
  }

  // The stored timer is updated later by runScheduledCallbacks(). For now,
  // only shorten the main-loop wait by the time that has already passed.
  if (elapsedTimeSinceSchedulerUpdate >= scheduledDelay) {
    return std::chrono::milliseconds::zero();
  }
  return scheduledDelay - elapsedTimeSinceSchedulerUpdate;
}

PythonExecutionResult MicroPythonRuntime::runScheduledCallbacks() {
  throwIfCalledFromAnotherThread();
  if (!m_previousSchedulerUpdateTime) {
    return {true, {}};
  }

  const auto currentTime = std::chrono::steady_clock::now();
  const auto elapsedTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - *m_previousSchedulerUpdateTime);
  // Keep fractions of a millisecond for the next update. Otherwise frequent
  // calls could slowly make Python timers lose time.
  *m_previousSchedulerUpdateTime += elapsedTime;

  const auto maximumElapsedMilliseconds =
      static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<std::uint32_t>::max());
  const std::uint32_t elapsedMilliseconds = elapsedTime.count() > maximumElapsedMilliseconds
                                                ? std::numeric_limits<std::uint32_t>::max()
                                                : static_cast<std::uint32_t>(elapsedTime.count());

  std::array<char, maximumCapturedTracebackSizeInBytes + 1U> tracebackBuffer{};
  const int                                                  succeeded =
      iot_scheduler_run_due_callbacks(elapsedMilliseconds, tracebackBuffer.data(), tracebackBuffer.size());
  return {succeeded != 0, tracebackBuffer.data()};
}

void MicroPythonRuntime::throwIfCalledFromAnotherThread() const {
  if (std::this_thread::get_id() != m_ownerThreadId) {
    throw std::logic_error("MicroPython must be used from the thread that created it");
  }
}

} // namespace python
} // namespace iot
