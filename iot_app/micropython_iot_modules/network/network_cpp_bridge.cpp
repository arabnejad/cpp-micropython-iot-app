#include "network_cpp_bridge.h"

#include "native_bridge_error_handler.h"
#include "iot/network/ifile_downloader.h"
#include "iot/python/micropython_application_context.h"

#include <stdexcept>
#include <string>

namespace {

thread_local iot::network::DownloadedFile latestDownloadedFile;
/* Download failures become RuntimeError messages in mod_iot_network.c. */
thread_local iot::python::internal::NativeBridgeErrorHandler nativeBridgeErrorHandler{"Unknown C++ network error"};

iot::python::MicroPythonApplicationContext &context() {
  auto *activeContext = iot::python::MicroPythonApplicationContext::active();
  if (activeContext == nullptr) {
    throw std::logic_error("Python network module is not connected to the application runtime");
  }
  return *activeContext;
}

} // namespace

extern "C" iot_native_result_t iot_network_download_file(const char *url, const char *expectedSha256,
                                                         iot_downloaded_file_t *downloadedFile) {
  return nativeBridgeErrorHandler.runSafely([=] {
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
