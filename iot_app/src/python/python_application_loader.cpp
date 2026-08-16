#include "iot/python/python_application_loader.h"

#include "iot/python/application_metadata.h"
#include "iot/python/path_validation.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace iot {
namespace python {
namespace {

constexpr std::size_t maximumApplicationMetadataSizeInBytes = 64U * 1024U;

std::string readFileWithLimit(const std::filesystem::path &filePath, std::size_t maximumSizeInBytes,
                              const char *fileDescription) {
  std::error_code error;
  const auto      fileSize = std::filesystem::file_size(filePath, error);
  if (error) {
    throw std::runtime_error("Could not read " + std::string(fileDescription) + " '" + filePath.string() +
                             "': " + error.message());
  }
  if (fileSize == 0U) {
    throw std::runtime_error(std::string(fileDescription) + " is empty: " + filePath.string());
  }
  if (fileSize > maximumSizeInBytes ||
      fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    throw std::runtime_error(std::string(fileDescription) + " is larger than the allowed limit: " + filePath.string());
  }

  std::ifstream inputFile(filePath, std::ios::binary);
  if (!inputFile) {
    throw std::runtime_error("Could not open " + std::string(fileDescription) + ": " + filePath.string());
  }

  std::string contents(static_cast<std::size_t>(fileSize), '\0');
  inputFile.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!inputFile) {
    throw std::runtime_error("Could not read the complete " + std::string(fileDescription) + ": " + filePath.string());
  }
  return contents;
}

} // namespace

PythonApplicationLoader::PythonApplicationLoader(std::size_t maximumSourceSizeInBytes)
    : maximumSourceSizeInBytes_(maximumSourceSizeInBytes) {
  if (maximumSourceSizeInBytes_ == 0U) {
    throw std::invalid_argument("Python application source-size limit must be greater than zero");
  }
}

PythonApplication PythonApplicationLoader::load(const std::filesystem::path &applicationDirectory) const {
  std::error_code error;
  const auto      canonicalApplicationDirectory = std::filesystem::canonical(applicationDirectory, error);
  if (error || !std::filesystem::is_directory(canonicalApplicationDirectory)) {
    throw std::runtime_error("Python application directory is not available: " + applicationDirectory.string());
  }

  const auto metadataPath = canonicalApplicationDirectory / "app.json";
  const auto metadataText =
      readFileWithLimit(metadataPath, maximumApplicationMetadataSizeInBytes, "application metadata");
  const auto metadata = parseApplicationMetadata(metadataText);

  const std::filesystem::path relativeEntryPoint(metadata.entryPoint);
  const auto                  canonicalEntryPoint =
      std::filesystem::canonical(canonicalApplicationDirectory / relativeEntryPoint, error);
  if (error || !std::filesystem::is_regular_file(canonicalEntryPoint)) {
    throw std::runtime_error("Application entry point is not a regular file: " +
                             (canonicalApplicationDirectory / relativeEntryPoint).string());
  }

  const auto pathInsidePackage = canonicalEntryPoint.lexically_relative(canonicalApplicationDirectory);
  if (!isSafeRelativePath(pathInsidePackage)) {
    throw std::runtime_error("Application entry point resolves outside its application directory");
  }

  auto sourceCode = readFileWithLimit(canonicalEntryPoint, maximumSourceSizeInBytes_, "application entry point");
  if (sourceCode.find('\0') != std::string::npos) {
    throw std::runtime_error("Application entry point contains a null byte and is not valid Python source");
  }

  return {metadata.applicationId, metadata.applicationName, canonicalApplicationDirectory, canonicalEntryPoint,
          std::move(sourceCode)};
}

} // namespace python
} // namespace iot
