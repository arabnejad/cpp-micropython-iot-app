#include "network_cpp_bridge.h"

#include "iot/network/ifile_downloader.h"
#include "iot/python/micropython_application_context.h"

#include <exception>
#include <stdexcept>
#include <string>

namespace {

thread_local std::string                  latestErrorMessage;
thread_local iot::network::DownloadedFile latestDownloadedFile;

iot_native_result_t success() noexcept {
  return {1, nullptr};
}

iot_native_result_t failure(const char *message) noexcept {
  latestErrorMessage = message;
  return {0, latestErrorMessage.c_str()};
}

iot::python::MicroPythonApplicationContext &context() {
  auto *activeContext = iot::python::MicroPythonApplicationContext::active();
  if (activeContext == nullptr) {
    throw std::logic_error("Python network module is not connected to the application runtime");
  }
  return *activeContext;
}

/*
 * Runs one C++ function and turns an exception into a result the MicroPython
 * C module can check.
 *
 * FunctionToRun is the type of the supplied lambda. For example:
 *
 *   return runSafely([=] {
 *     context().fileDownloader().downloadFile({url, expectedSha256});
 *   });
 *
 * Calling functionToRun() runs the code inside that lambda. The template
 * accepts the lambda directly without first wrapping it in std::function.
 */
template <typename FunctionToRun> iot_native_result_t runSafely(FunctionToRun functionToRun) noexcept {
  try {
    functionToRun();
    return success();
  } catch (const std::exception &error) {
    return failure(error.what());
  } catch (...) {
    return failure("Unknown C++ network error");
  }
}

} // namespace

extern "C" iot_native_result_t iot_network_download_file(const char *url, const char *expectedSha256,
                                                         iot_downloaded_file_t *downloadedFile) {
  return runSafely([=] {
    if (url == nullptr || expectedSha256 == nullptr || downloadedFile == nullptr) {
      throw std::invalid_argument("Download URL, expected SHA-256, and result output are required");
    }

    latestDownloadedFile              = context().fileDownloader().downloadFile({url, expectedSha256});
    downloadedFile->file_path         = latestDownloadedFile.filePath.c_str();
    downloadedFile->sha256            = latestDownloadedFile.sha256.c_str();
    downloadedFile->size_in_bytes     = latestDownloadedFile.sizeInBytes;
    downloadedFile->content_type      = latestDownloadedFile.contentType.c_str();
    downloadedFile->loaded_from_cache = latestDownloadedFile.loadedFromCache ? 1 : 0;
  });
}
