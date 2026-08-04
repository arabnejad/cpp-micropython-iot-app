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

/* Result of running Python startup code or scheduled callbacks. */
struct PythonExecutionResult {
  bool succeeded{false};
  /* Contains the end of the traceback when succeeded is false. */
  std::string traceback;
};

/*
 * Starts and controls the MicroPython interpreter for one Python application.
 *
 * PythonApplicationManager creates one MicroPythonRuntime for each application
 * it starts. Creating the runtime reserves memory for the interpreter and
 * starts MicroPython. The manager then calls executeApplication() to run
 * main.py. After main.py returns, the manager calls runScheduledCallbacks()
 * from its main loop so the application's timers can continue to run. The
 * runtime records the time between those calls and uses it to update the
 * timers.
 *
 * When this object is destroyed, it stops MicroPython and frees the memory
 * reserved for the interpreter. If an exception leaves the current scope,
 * C++ destroys the object automatically, so the interpreter is still stopped
 * and its memory is still released.
 *
 * MicroPython must be started, used, and stopped on the same thread. The
 * execution and scheduler functions reject calls from any other thread.
 */
class MicroPythonRuntime {
public:
  /* Starts an interpreter with the requested heap size. */
  MicroPythonRuntime(std::size_t heapSizeInBytes);
  ~MicroPythonRuntime();

  // Owns one interpreter and heap; copying and moving are disabled.
  MicroPythonRuntime(const MicroPythonRuntime &)            = delete;
  MicroPythonRuntime &operator=(const MicroPythonRuntime &) = delete;
  MicroPythonRuntime(MicroPythonRuntime &&)                 = delete;
  MicroPythonRuntime &operator=(MicroPythonRuntime &&)      = delete;

  /* Parses, compiles, and runs the application's entry point. */
  PythonExecutionResult executeApplication(const PythonApplication &pythonApplication);

  /*
   * Gets the time until Python's next scheduled callback.
   *
   * The MicroPython scheduler stores a remaining delay. This function reduces
   * that delay by the time that has passed since the scheduler was last
   * updated. That time can include work done inside the previous callback.
   *
   * For example, suppose the scheduler reports a 1,000 ms delay and the
   * previous callback took 300 ms:
   *
   *   Stored delay:             1,000 ms
   *   Time already passed:       -300 ms
   *   Main-loop wait returned:    700 ms
   *
   * This function only calculates the wait. It does not change the stored
   * timer. runScheduledCallbacks() records the complete elapsed time later, so
   * the same 300 ms is not counted twice.
   */
  std::optional<std::chrono::milliseconds> timeUntilNextScheduledCallback() const;

  /* Advances Python timers and runs callbacks that are now due. */
  PythonExecutionResult runScheduledCallbacks();

private:
  /* Throws if the caller is not the thread that created this interpreter. */
  void throwIfCalledFromAnotherThread() const;

  logging::Logger m_logger{"MicroPythonRuntime"};
  // Storing the heap as machine words gives MicroPython the alignment it needs.
  std::vector<std::uintptr_t>                          m_heapWords;
  std::thread::id                                      m_ownerThreadId;
  std::optional<std::chrono::steady_clock::time_point> m_previousSchedulerUpdateTime;
};

} // namespace python
} // namespace iot
