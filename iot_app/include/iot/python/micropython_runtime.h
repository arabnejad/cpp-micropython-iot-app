#pragma once

#include "iot/logging/logger.h"
#include "iot/python/python_application.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

namespace iot {
namespace python {

/** Result of running Python startup code or scheduled callbacks. */
struct PythonExecutionResult {
  bool succeeded{false};
  /** Contains the end of the traceback when `succeeded` is false. */
  std::string traceback;
};

/**
 * Starts and controls the MicroPython interpreter for one Python application.
 *
 * PythonApplicationRunner creates this object when it starts an application.
 * The constructor allocates the Python heap and starts MicroPython. The runner
 * then calls executeApplication() to run the application's main.py and calls
 * runScheduledCallbacks() while the application is running.
 *
 * The runner destroys this object when the application stops, fails, or is
 * replaced by another application. The destructor shuts down MicroPython and
 * releases its heap, including when C++ is leaving a scope because of an
 * exception.
 *
 * MicroPython must be started, used, and stopped on the same thread. The
 * execution and scheduler functions reject calls from any other thread.
 */
class MicroPythonRuntime {
public:
  /** Starts an interpreter with at least `heapSizeInBytes` of heap memory. */
  MicroPythonRuntime(std::size_t heapSizeInBytes);
  ~MicroPythonRuntime();

  // Owns one interpreter and heap; copying and moving are disabled.
  MicroPythonRuntime(const MicroPythonRuntime &)            = delete;
  MicroPythonRuntime &operator=(const MicroPythonRuntime &) = delete;
  MicroPythonRuntime(MicroPythonRuntime &&)                 = delete;
  MicroPythonRuntime &operator=(MicroPythonRuntime &&)      = delete;

  /** Parses, compiles, and runs one application's entry-point source. */
  PythonExecutionResult executeApplication(const PythonApplication &pythonApplication);

  /** Gets the time until Python's next scheduled callback. */
  std::optional<std::chrono::milliseconds> timeUntilNextScheduledCallback() const;

  /** Advances Python timers and runs callbacks that are now due. */
  PythonExecutionResult runScheduledCallbacks(std::chrono::milliseconds elapsedTime);

private:
  /** Throws if the caller is not the thread that created this interpreter. */
  void throwIfCalledFromAnotherThread() const;

  logging::Logger logger_{"MicroPythonRuntime"};
  // Storing the heap as machine words gives MicroPython the alignment it needs.
  std::vector<std::uintptr_t> heapWords_;
  std::thread::id             ownerThreadId_;
};

} // namespace python
} // namespace iot
