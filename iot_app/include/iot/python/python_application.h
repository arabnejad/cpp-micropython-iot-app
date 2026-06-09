#pragma once

#include <filesystem>
#include <string>

namespace iot {
namespace python {

/*
 * A checked Python application that is ready to run.
 *
 * For the default application, the loader creates this object from files on
 * disk. For an application received through MQTT, the temporary installer
 * creates it from the validated deployment request. The object holds the
 * application details and Python source code needed to start MicroPython.
 */
struct PythonApplication {
  /* Stable ID used to distinguish this app from other apps. */
  std::string applicationId;
  /* Name shown in logs, status messages, and the dashboard. */
  std::string applicationName;
  /* Absolute, normalized path to the application directory. */
  std::filesystem::path packageDirectory;
  /* Absolute, normalized path to the Python entry-point file. */
  std::filesystem::path entryPointPath;
  std::string           sourceCode;
};

} // namespace python
} // namespace iot
