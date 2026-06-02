#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_message.h"
#include "iot/python/python_application.h"
#include "iot/python/python_application_loader.h"

#include <filesystem>

namespace iot {
namespace python {

/**
 * Writes received applications under `/tmp` and loads them.
 *
 * `iot_app` clears this directory each time it starts. Received apps disappear
 * after a restart or reboot. Only the shipped default app is permanent.
 */
class TemporaryPythonApplicationInstaller {
public:
  TemporaryPythonApplicationInstaller(const PythonApplicationLoader &applicationLoader,
                                      std::filesystem::path          temporaryRootDirectory);

  // Manages one temporary directory; copying and moving are disabled.
  TemporaryPythonApplicationInstaller(const TemporaryPythonApplicationInstaller &)            = delete;
  TemporaryPythonApplicationInstaller &operator=(const TemporaryPythonApplicationInstaller &) = delete;
  TemporaryPythonApplicationInstaller(TemporaryPythonApplicationInstaller &&)                 = delete;
  TemporaryPythonApplicationInstaller &operator=(TemporaryPythonApplicationInstaller &&)      = delete;

  /** Writes one received app to `/tmp`, validates it, and loads it. */
  PythonApplication installAndLoad(const messaging::ApplicationDeploymentRequest &deploymentRequest);

  /** Removes a received app that is no longer needed. */
  void removeInstalledApplication(const std::filesystem::path &applicationDirectory) noexcept;

private:
  logging::Logger                logger_{"TemporaryPythonApplicationInstaller"};
  const PythonApplicationLoader &applicationLoader_;
  std::filesystem::path          temporaryRootDirectory_;
};

/** Builds this user's temporary app path: `/tmp/iot-app-<uid>/applications`. */
std::filesystem::path defaultTemporaryApplicationRoot();

} // namespace python
} // namespace iot
