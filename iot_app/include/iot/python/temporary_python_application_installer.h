#pragma once

#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_message.h"
#include "iot/python/python_application.h"

#include <filesystem>

namespace iot {
namespace python {

/*
 * Installs/Writes validated MQTT applications in a private /tmp directory.
 *
 * IoT App clears the directory on startup. Received applications therefore
 * disappear after a restart or reboot; the shipped default app is permanent.
 */
class TemporaryPythonApplicationInstaller {
public:
  explicit TemporaryPythonApplicationInstaller(std::filesystem::path temporaryRootDirectory);

  // Manages one temporary directory; copying and moving are disabled.
  TemporaryPythonApplicationInstaller(const TemporaryPythonApplicationInstaller &)            = delete;
  TemporaryPythonApplicationInstaller &operator=(const TemporaryPythonApplicationInstaller &) = delete;
  TemporaryPythonApplicationInstaller(TemporaryPythonApplicationInstaller &&)                 = delete;
  TemporaryPythonApplicationInstaller &operator=(TemporaryPythonApplicationInstaller &&)      = delete;

  /* Writes one deployment to /tmp and returns the application ready to run. */
  PythonApplication installApplication(const messaging::ApplicationDeploymentRequest &deploymentRequest);

  /* Removes a received app that is no longer needed. */
  void removeInstalledApplication(const std::filesystem::path &applicationDirectory) noexcept;

private:
  logging::Logger       m_logger{"TemporaryPythonApplicationInstaller"};
  std::filesystem::path m_temporaryRootDirectory;
};

/*
 * Returns the directory used to store applications received through MQTT.
 * Each Linux user gets a separate directory. For example, user ID 1000 uses
 * /tmp/iot-app-1000/applications.
 */
std::filesystem::path defaultTemporaryApplicationRoot();

} // namespace python
} // namespace iot
