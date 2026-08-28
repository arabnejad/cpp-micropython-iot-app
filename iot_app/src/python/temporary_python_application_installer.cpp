#include "iot/python/temporary_python_application_installer.h"
#include "iot/python/path_validation.h"

#include <cjson/cJSON.h>

#include <sys/stat.h>
#include <unistd.h>

#include <exception>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace iot {
namespace python {
namespace {

struct CJsonObjectDeleter {
  void operator()(cJSON *jsonObject) const noexcept {
    cJSON_Delete(jsonObject);
  }
};

using UniqueCJsonObject = std::unique_ptr<cJSON, CJsonObjectDeleter>;

void throwIfPathIsUnsafe(const std::filesystem::path &path, const char *description) {
  if (!isSafeRelativePath(path)) {
    throw std::runtime_error(std::string(description) + " is not a safe relative path");
  }
}

void writePrivateFile(const std::filesystem::path &filePath, const std::string &contents) {
  std::ofstream outputFile(filePath, std::ios::binary | std::ios::trunc);
  if (!outputFile) {
    throw std::runtime_error("Could not create temporary application file: " + filePath.string());
  }
  outputFile.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  outputFile.close();
  if (!outputFile) {
    throw std::runtime_error("Could not write temporary application file: " + filePath.string());
  }
  if (::chmod(filePath.c_str(), S_IRUSR | S_IWUSR) != 0) {
    throw std::runtime_error("Could not protect temporary application file: " + filePath.string());
  }
}

std::string createApplicationMetadataJson(const messaging::ApplicationDeploymentRequest &deploymentRequest) {
  UniqueCJsonObject metadata{cJSON_CreateObject()};
  if (!metadata || cJSON_AddStringToObject(metadata.get(), "id", deploymentRequest.applicationId.c_str()) == nullptr ||
      cJSON_AddStringToObject(metadata.get(), "name", deploymentRequest.applicationName.c_str()) == nullptr ||
      cJSON_AddStringToObject(metadata.get(), "entry_point", deploymentRequest.entryPoint.c_str()) == nullptr) {
    throw std::runtime_error("Could not create the temporary application metadata");
  }

  char *serializedMetadata = cJSON_Print(metadata.get());
  if (serializedMetadata == nullptr) {
    throw std::runtime_error("Could not serialize the temporary application metadata");
  }
  std::string metadataJson(serializedMetadata);
  cJSON_free(serializedMetadata);
  metadataJson.push_back('\n');
  return metadataJson;
}

void createPrivateDirectory(const std::filesystem::path &directory) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error || !std::filesystem::is_directory(directory)) {
    throw std::runtime_error("Could not create temporary application directory: " + directory.string());
  }
  if (::chmod(directory.c_str(), S_IRWXU) != 0) {
    throw std::runtime_error("Could not protect temporary application directory: " + directory.string());
  }
}

void removeFailedInstallationFiles(const std::filesystem::path &stagingDirectory,
                                   const std::filesystem::path &installedDirectory,
                                   bool                         installedDirectoryWasCreated) noexcept {
  std::error_code ignoredError;
  std::filesystem::remove_all(stagingDirectory, ignoredError);
  if (installedDirectoryWasCreated) {
    ignoredError.clear();
    std::filesystem::remove_all(installedDirectory, ignoredError);
  }
}

} // namespace

TemporaryPythonApplicationInstaller::TemporaryPythonApplicationInstaller(std::filesystem::path temporaryRootDirectory)
    : m_temporaryRootDirectory(std::move(temporaryRootDirectory)) {
  if (m_temporaryRootDirectory.empty()) {
    IOT_LOG_ERROR(m_logger, "Cannot create installer because temporaryRootDirectory is empty");
    throw std::invalid_argument("Temporary application installer requires a root directory");
  }

  std::error_code error;
  const auto      existingStatus = std::filesystem::symlink_status(m_temporaryRootDirectory, error);
  if (!error && std::filesystem::exists(existingStatus)) {
    if (std::filesystem::is_symlink(existingStatus) || !std::filesystem::is_directory(existingStatus)) {
      throw std::runtime_error("Temporary application root is not a normal directory: " +
                               m_temporaryRootDirectory.string());
    }
    std::filesystem::remove_all(m_temporaryRootDirectory, error);
    if (error) {
      throw std::runtime_error("Could not clear old temporary applications: " + error.message());
    }
  }
  createPrivateDirectory(m_temporaryRootDirectory);
}

PythonApplication TemporaryPythonApplicationInstaller::installApplication(
    const messaging::ApplicationDeploymentRequest &deploymentRequest) {
  const std::filesystem::path transferDirectoryName(deploymentRequest.transferId);
  const std::filesystem::path entryPointPath(deploymentRequest.entryPoint);
  throwIfPathIsUnsafe(transferDirectoryName, "Deployment transfer ID");
  throwIfPathIsUnsafe(entryPointPath, "Application entry point");

  const auto stagingDirectory   = m_temporaryRootDirectory / (".staging-" + deploymentRequest.transferId);
  const auto installedDirectory = m_temporaryRootDirectory / deploymentRequest.transferId;
  IOT_LOG_DEBUG(m_logger, "Installing application id=", deploymentRequest.applicationId,
                ", transferId=", deploymentRequest.transferId, ", stagingDirectory=", stagingDirectory,
                ", installedDirectory=", installedDirectory, ", sourceBytes=", deploymentRequest.sourceCode.size());
  std::error_code error;
  std::filesystem::remove_all(stagingDirectory, error);
  if (error) {
    throw std::runtime_error("Could not clear the deployment staging directory: " + error.message());
  }

  bool installedDirectoryWasCreated = false;
  try {
    createPrivateDirectory(stagingDirectory);
    const auto fullEntryPointPath = stagingDirectory / entryPointPath;
    if (entryPointPath.has_parent_path()) {
      createPrivateDirectory(fullEntryPointPath.parent_path());
    }
    writePrivateFile(stagingDirectory / "app.json", createApplicationMetadataJson(deploymentRequest));
    writePrivateFile(fullEntryPointPath, deploymentRequest.sourceCode);

    std::filesystem::remove_all(installedDirectory, error);
    if (error) {
      throw std::runtime_error("Could not replace an earlier copy of this deployment: " + error.message());
    }
    std::filesystem::rename(stagingDirectory, installedDirectory, error);
    if (error) {
      throw std::runtime_error("Could not activate the temporary application directory: " + error.message());
    }
    installedDirectoryWasCreated = true;

    // The MQTT parser has already checked the metadata and source. Build the
    // application from those values instead of opening and parsing the files
    // that were just written.
    const auto normalizedInstalledDirectory = std::filesystem::canonical(installedDirectory, error);
    if (error) {
      throw std::runtime_error("Could not resolve the installed application directory: " + error.message());
    }
    const auto normalizedEntryPointPath = std::filesystem::canonical(installedDirectory / entryPointPath, error);
    if (error) {
      throw std::runtime_error("Could not resolve the installed application entry point: " + error.message());
    }
    return {deploymentRequest.applicationId, deploymentRequest.applicationName, normalizedInstalledDirectory,
            normalizedEntryPointPath, deploymentRequest.sourceCode};
  } catch (const std::exception &caughtError) {
    IOT_LOG_ERROR(m_logger,
                  "Temporary application installation failed; applicationId=", deploymentRequest.applicationId,
                  ", transferId=", deploymentRequest.transferId, ", stagingDirectory=", stagingDirectory,
                  ", installedDirectory=", installedDirectory, ": ", caughtError.what());
    removeFailedInstallationFiles(stagingDirectory, installedDirectory, installedDirectoryWasCreated);
    throw;
  } catch (...) {
    IOT_LOG_ERROR(m_logger, "Temporary application installation failed with an unknown exception; applicationId=",
                  deploymentRequest.applicationId, ", transferId=", deploymentRequest.transferId,
                  ", stagingDirectory=", stagingDirectory, ", installedDirectory=", installedDirectory);
    removeFailedInstallationFiles(stagingDirectory, installedDirectory, installedDirectoryWasCreated);
    throw;
  }
}

void TemporaryPythonApplicationInstaller::removeInstalledApplication(
    const std::filesystem::path &applicationDirectory) noexcept {
  const auto relativePath = applicationDirectory.lexically_relative(m_temporaryRootDirectory);
  if (!isSafeRelativePath(relativePath) || relativePath.has_parent_path()) {
    IOT_LOG_WARNING(m_logger, "Refused to remove application directory outside the temporary root; directory=",
                    applicationDirectory, ", temporaryRoot=", m_temporaryRootDirectory);
    return;
  }
  std::error_code error;
  std::filesystem::remove_all(applicationDirectory, error);
  if (error) {
    IOT_LOG_WARNING(m_logger, "Could not remove temporary application directory ", applicationDirectory, ": ",
                    error.message());
  } else {
    IOT_LOG_DEBUG(m_logger, "Removed temporary application directory ", applicationDirectory);
  }
}

std::filesystem::path defaultTemporaryApplicationRoot() {
  // Use /tmp directly. Do not allow an environment variable to move received
  // apps into permanent storage.
  return std::filesystem::path{"/tmp"} / ("iot-app-" + std::to_string(static_cast<unsigned long>(::getuid()))) /
         "applications";
}

} // namespace python
} // namespace iot
