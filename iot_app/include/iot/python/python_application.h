#pragma once

#include <filesystem>
#include <string>

namespace iot {
namespace python {

/**
 * A Python application that has been fully loaded from disk.
 *
 * This object keeps its own copy of the source, so the runner does not depend
 * on a file or temporary buffer remaining available.
 */
struct PythonApplication {
  /** Stable ID used to distinguish this app from other apps. */
  std::string applicationId;
  /** Name shown in logs, status messages, and the dashboard. */
  std::string applicationName;
  /** Absolute, normalized path to the application directory. */
  std::filesystem::path packageDirectory;
  /** Absolute, normalized path to the Python entry-point file. */
  std::filesystem::path entryPointPath;
  /** Complete source code read from the entry-point file. */
  std::string sourceCode;
};

} // namespace python
} // namespace iot
