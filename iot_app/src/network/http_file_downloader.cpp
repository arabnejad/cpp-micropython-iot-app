#include "iot/network/http_file_downloader.h"

#include "checksum/sha256.h"

#include <curl/curl.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace iot {
namespace network {
namespace {

constexpr long maximumRedirectCount = 5L;

struct CurlHandleDeleter {
  void operator()(CURL *curlHandle) const noexcept {
    curl_easy_cleanup(curlHandle);
  }
};

using UniqueCurlHandle = std::unique_ptr<CURL, CurlHandleDeleter>;

class TemporaryDownloadFile {
public:
  explicit TemporaryDownloadFile(const std::filesystem::path &downloadCacheDirectory) {
    std::string       filenameTemplate = (downloadCacheDirectory / ".download-XXXXXX").string();
    std::vector<char> writableFilename(filenameTemplate.begin(), filenameTemplate.end());
    writableFilename.push_back('\0');

    const int fileDescriptor = ::mkstemp(writableFilename.data());
    if (fileDescriptor < 0) {
      throw std::runtime_error("Could not create a temporary download file");
    }
    try {
      m_filePath = writableFilename.data();
    } catch (...) {
      // The constructor has not finished, so its destructor cannot clean up.
      ::close(fileDescriptor);
      ::unlink(writableFilename.data());
      throw;
    }
    m_outputFile = ::fdopen(fileDescriptor, "wb");
    if (m_outputFile == nullptr) {
      ::close(fileDescriptor);
      std::error_code ignoredError;
      std::filesystem::remove(m_filePath, ignoredError);
      throw std::runtime_error("Could not open the temporary download file for writing");
    }
  }

  ~TemporaryDownloadFile() {
    close();
    std::error_code ignoredError;
    if (!m_filePath.empty()) {
      std::filesystem::remove(m_filePath, ignoredError);
    }
  }

  TemporaryDownloadFile(const TemporaryDownloadFile &)            = delete;
  TemporaryDownloadFile &operator=(const TemporaryDownloadFile &) = delete;
  TemporaryDownloadFile(TemporaryDownloadFile &&)                 = delete;
  TemporaryDownloadFile &operator=(TemporaryDownloadFile &&)      = delete;

  std::FILE *outputFile() const noexcept {
    return m_outputFile;
  }

  const std::filesystem::path &filePath() const noexcept {
    return m_filePath;
  }

  void closeForReading() {
    if (m_outputFile == nullptr) {
      return;
    }
    std::FILE *outputFile = m_outputFile;
    m_outputFile          = nullptr;
    if (std::fclose(outputFile) != 0) {
      throw std::runtime_error("Could not finish writing the downloaded file");
    }
  }

  /* Keep cleaning up the file after renaming it, until its cache record is saved. */
  void moveToCache(std::filesystem::path finalFilePath) {
    std::filesystem::rename(m_filePath, finalFilePath);
    m_filePath = std::move(finalFilePath);
  }

  void keepCachedFile() noexcept {
    m_filePath.clear();
  }

private:
  void close() noexcept {
    if (m_outputFile != nullptr) {
      std::fclose(m_outputFile);
      m_outputFile = nullptr;
    }
  }

  std::filesystem::path m_filePath;
  std::FILE            *m_outputFile{nullptr};
};

struct DownloadWriteState {
  std::FILE  *outputFile{nullptr};
  std::size_t maximumFileSizeInBytes{0};
  std::size_t receivedBytes{0};
  bool        maximumSizeWasExceeded{false};
  bool        fileWriteFailed{false};
};

std::size_t writeDownloadedBytes(char *receivedData, std::size_t sizeOfEachDataItem, std::size_t numberOfDataItems,
                                 void *writeStatePointer) {
  auto &writeState = *static_cast<DownloadWriteState *>(writeStatePointer);
  if (sizeOfEachDataItem != 0U && numberOfDataItems > std::numeric_limits<std::size_t>::max() / sizeOfEachDataItem) {
    writeState.maximumSizeWasExceeded = true;
    return 0U;
  }
  const std::size_t receivedByteCount = sizeOfEachDataItem * numberOfDataItems;
  if (writeState.receivedBytes > writeState.maximumFileSizeInBytes ||
      receivedByteCount > writeState.maximumFileSizeInBytes - writeState.receivedBytes) {
    writeState.maximumSizeWasExceeded = true;
    return 0U;
  }
  if (std::fwrite(receivedData, 1U, receivedByteCount, writeState.outputFile) != receivedByteCount) {
    writeState.fileWriteFailed = true;
    return 0U;
  }
  writeState.receivedBytes += receivedByteCount;
  return receivedByteCount;
}

void throwIfCurlOptionFailed(CURLcode curlResult, const char *optionName) {
  if (curlResult != CURLE_OK) {
    throw std::runtime_error(std::string("Could not configure libcurl option ") + optionName + ": " +
                             curl_easy_strerror(curlResult));
  }
}

std::string convertTextToLowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return text;
}

std::string validateAndNormalizeExpectedSha256(const std::string &expectedSha256) {
  if (expectedSha256.empty()) {
    return {};
  }
  if (expectedSha256.size() != 64U) {
    throw std::invalid_argument("expected_sha256 must contain exactly 64 hexadecimal characters");
  }
  for (const char character : expectedSha256) {
    if (!std::isxdigit(static_cast<unsigned char>(character))) {
      throw std::invalid_argument("expected_sha256 must contain only hexadecimal characters");
    }
  }
  return convertTextToLowercase(expectedSha256);
}

void validateDownloadUrl(const std::string &url) {
  if (url.empty()) {
    throw std::invalid_argument("Download URL is empty");
  }
  if (url.rfind("https://", 0U) != 0U && url.rfind("http://", 0U) != 0U) {
    throw std::invalid_argument("Download URL must start with https:// or http://");
  }
}

void createPrivateCacheDirectory(const std::filesystem::path &downloadCacheDirectory) {
  std::error_code filesystemError;
  std::filesystem::create_directories(downloadCacheDirectory, filesystemError);
  if (filesystemError || !std::filesystem::is_directory(downloadCacheDirectory)) {
    throw std::runtime_error("Could not create download cache directory: " + downloadCacheDirectory.string());
  }
  if (::chmod(downloadCacheDirectory.c_str(), S_IRWXU) != 0) {
    throw std::runtime_error("Could not protect download cache directory: " + downloadCacheDirectory.string());
  }
}

std::string calculateDownloadedFileSha256(const std::filesystem::path &filePath) {
  try {
    return internal::calculateFileSha256(filePath);
  } catch (const internal::Sha256CalculationError &error) {
    switch (error.failure()) {
    case internal::Sha256Failure::FileCouldNotBeOpened:
      throw std::runtime_error("Could not read downloaded file while calculating SHA-256");
    case internal::Sha256Failure::CalculationCouldNotStart:
      throw std::runtime_error("OpenSSL could not start the downloaded file SHA-256 calculation");
    case internal::Sha256Failure::BytesCouldNotBeProcessed:
      throw std::runtime_error("OpenSSL could not update the downloaded file SHA-256 calculation");
    case internal::Sha256Failure::FileCouldNotBeRead:
      throw std::runtime_error("Could not finish reading downloaded file while calculating SHA-256");
    case internal::Sha256Failure::CalculationCouldNotFinish:
      throw std::runtime_error("OpenSSL could not finish the downloaded file SHA-256 calculation");
    }
  }
  throw std::runtime_error("OpenSSL could not finish the downloaded file SHA-256 calculation");
}

} // namespace

HttpFileDownloader::HttpFileDownloader(std::filesystem::path  downloadCacheDirectory,
                                       HttpFileDownloadLimits downloadLimits)
    : m_downloadCacheDirectory(std::move(downloadCacheDirectory)), m_downloadLimits(downloadLimits) {
  if (m_downloadCacheDirectory.empty()) {
    throw std::invalid_argument("HTTP file downloader requires a cache directory");
  }
  if (m_downloadLimits.maximumFileSizeInBytes == 0U ||
      m_downloadLimits.maximumStoredDownloadedFilesSizeInBytes < m_downloadLimits.maximumFileSizeInBytes ||
      m_downloadLimits.connectionTimeout.count() <= 0 || m_downloadLimits.totalTimeout.count() <= 0) {
    throw std::invalid_argument(
        "HTTP file downloader requires positive timeouts and an application limit at least as large as one file");
  }
  createPrivateCacheDirectory(m_downloadCacheDirectory);
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    throw std::runtime_error("Could not initialize libcurl");
  }
}

HttpFileDownloader::~HttpFileDownloader() {
  curl_global_cleanup();
}

DownloadedFile HttpFileDownloader::downloadFile(const FileDownloadRequest &fileDownloadRequest) {
  validateDownloadUrl(fileDownloadRequest.url);
  const std::string expectedSha256 = validateAndNormalizeExpectedSha256(fileDownloadRequest.expectedSha256);

  if (auto cachedDownload = findValidCachedDownload(expectedSha256)) {
    cachedDownload->loadedFromCache = true;
    IOT_LOG_DEBUG(m_logger, "Using cached download; file=", cachedDownload->filePath);
    return *cachedDownload;
  }

  if (m_storedDownloadedFileBytes >= m_downloadLimits.maximumStoredDownloadedFilesSizeInBytes) {
    throw std::runtime_error("This Python application has reached its stored-download limit of " +
                             std::to_string(m_downloadLimits.maximumStoredDownloadedFilesSizeInBytes) + " bytes");
  }
  const std::size_t remainingStoredDownloadBytes =
      m_downloadLimits.maximumStoredDownloadedFilesSizeInBytes - m_storedDownloadedFileBytes;
  const std::size_t maximumBytesForThisTransfer =
      std::min(m_downloadLimits.maximumFileSizeInBytes, remainingStoredDownloadBytes);

  // With signals disabled, a blocking DNS resolver can ignore the timeout.
  // Require libcurl's threaded or c-ares resolver before starting a transfer.
  // https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html
  const auto *curlVersion = curl_version_info(CURLVERSION_NOW);
  if (curlVersion == nullptr || (curlVersion->features & CURL_VERSION_ASYNCHDNS) == 0) {
    throw std::runtime_error("Downloads require libcurl with asynchronous DNS to enforce the transfer timeout");
  }

  TemporaryDownloadFile temporaryDownloadFile(m_downloadCacheDirectory);
  UniqueCurlHandle      curlHandle(curl_easy_init());
  if (!curlHandle) {
    throw std::runtime_error("Could not create a libcurl download handle");
  }

  DownloadWriteState writeState{temporaryDownloadFile.outputFile(), maximumBytesForThisTransfer, 0U, false, false};
  std::array<char, CURL_ERROR_SIZE> curlErrorMessage{};

  // Restrict transfers to the two protocols exposed by the Python API. TLS
  // certificate and host-name checks stay enabled for HTTPS downloads.
  // https://curl.se/libcurl/c/CURLOPT_PROTOCOLS_STR.html
  // https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
  // https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_URL, fileDownloadRequest.url.c_str()), "URL");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_PROTOCOLS_STR, "http,https"), "PROTOCOLS_STR");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https"),
                          "REDIR_PROTOCOLS_STR");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_FOLLOWLOCATION, 1L), "FOLLOWLOCATION");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_MAXREDIRS, maximumRedirectCount), "MAXREDIRS");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_FAILONERROR, 1L), "FAILONERROR");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_NOSIGNAL, 1L), "NOSIGNAL");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_SSL_VERIFYPEER, 1L), "SSL_VERIFYPEER");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_SSL_VERIFYHOST, 2L), "SSL_VERIFYHOST");
  throwIfCurlOptionFailed(
      curl_easy_setopt(curlHandle.get(), CURLOPT_CONNECTTIMEOUT_MS, m_downloadLimits.connectionTimeout.count()),
      "CONNECTTIMEOUT_MS");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_TIMEOUT_MS, m_downloadLimits.totalTimeout.count()),
                          "TIMEOUT_MS");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_MAXFILESIZE_LARGE,
                                           static_cast<curl_off_t>(m_downloadLimits.maximumFileSizeInBytes)),
                          "MAXFILESIZE_LARGE");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_WRITEFUNCTION, writeDownloadedBytes),
                          "WRITEFUNCTION");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_WRITEDATA, &writeState), "WRITEDATA");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_ERRORBUFFER, curlErrorMessage.data()),
                          "ERRORBUFFER");
  throwIfCurlOptionFailed(curl_easy_setopt(curlHandle.get(), CURLOPT_USERAGENT, "iot-app/0.1"), "USERAGENT");

  const CURLcode curlDownloadResult = curl_easy_perform(curlHandle.get());
  if (writeState.maximumSizeWasExceeded) {
    if (maximumBytesForThisTransfer < m_downloadLimits.maximumFileSizeInBytes) {
      throw std::runtime_error("This Python application has reached its stored-download limit of " +
                               std::to_string(m_downloadLimits.maximumStoredDownloadedFilesSizeInBytes) + " bytes");
    }
    throw std::runtime_error("Downloaded file is larger than the allowed limit of " +
                             std::to_string(m_downloadLimits.maximumFileSizeInBytes) + " bytes");
  }
  if (writeState.fileWriteFailed) {
    throw std::runtime_error("Could not write all downloaded bytes to the temporary file");
  }
  if (curlDownloadResult == CURLE_OPERATION_TIMEDOUT) {
    // Either limit can cause this error. Do not claim that the full transfer
    // time elapsed when connecting to the server may have timed out first.
    throw std::runtime_error(
        "File download timed out (connection limit: " + std::to_string(m_downloadLimits.connectionTimeout.count()) +
        " ms; transfer limit: " + std::to_string(m_downloadLimits.totalTimeout.count()) +
        " ms): " + (curlErrorMessage[0] == '\0' ? curl_easy_strerror(curlDownloadResult) : curlErrorMessage.data()));
  }
  if (curlDownloadResult != CURLE_OK) {
    const std::string curlErrorDetails =
        curlErrorMessage[0] == '\0' ? curl_easy_strerror(curlDownloadResult) : curlErrorMessage.data();
    throw std::runtime_error("File download failed: " + curlErrorDetails);
  }
  if (writeState.receivedBytes == 0U) {
    throw std::runtime_error("File download returned an empty file");
  }

  char *curlReportedContentType = nullptr;
  if (curl_easy_getinfo(curlHandle.get(), CURLINFO_CONTENT_TYPE, &curlReportedContentType) != CURLE_OK) {
    curlReportedContentType = nullptr;
  }
  const std::string contentType =
      curlReportedContentType == nullptr ? "application/octet-stream" : curlReportedContentType;

  temporaryDownloadFile.closeForReading();

  const std::string calculatedSha256 = calculateDownloadedFileSha256(temporaryDownloadFile.filePath());
  if (!expectedSha256.empty() && calculatedSha256 != expectedSha256) {
    throw std::runtime_error("Downloaded file does not match expected_sha256");
  }

  if (auto existingDownload = findValidCachedDownload(calculatedSha256)) {
    existingDownload->contentType     = contentType;
    existingDownload->loadedFromCache = false;
    return *existingDownload;
  }

  const std::filesystem::path finalFilePath = m_downloadCacheDirectory / (calculatedSha256 + ".download");
  std::error_code             filesystemError;
  if (std::filesystem::exists(finalFilePath, filesystemError) && !filesystemError) {
    std::filesystem::remove(finalFilePath, filesystemError);
  }
  if (filesystemError) {
    throw std::runtime_error("Could not place downloaded file in the cache: " + filesystemError.message());
  }
  if (::chmod(temporaryDownloadFile.filePath().c_str(), S_IRUSR | S_IWUSR) != 0) {
    throw std::runtime_error("Could not protect cached download: " + finalFilePath.string());
  }

  DownloadedFile downloadedFile{finalFilePath, calculatedSha256, writeState.receivedBytes, contentType, false};
  temporaryDownloadFile.moveToCache(finalFilePath);
  // If saving the record throws, the temporary-file owner removes the renamed
  // file too. Only count bytes after the file and its record both exist.
  m_downloadedFilesBySha256.emplace(calculatedSha256, downloadedFile);
  m_storedDownloadedFileBytes += writeState.receivedBytes;
  temporaryDownloadFile.keepCachedFile();
  IOT_LOG_INFO(m_logger, "Downloaded file; bytes=", writeState.receivedBytes, ", sha256=", calculatedSha256,
               ", cacheFile=", finalFilePath);
  return downloadedFile;
}

std::optional<DownloadedFile> HttpFileDownloader::findValidCachedDownload(const std::string &sha256) {
  const auto cachedDownload = m_downloadedFilesBySha256.find(sha256);
  if (cachedDownload == m_downloadedFilesBySha256.end()) {
    return std::nullopt;
  }

  const DownloadedFile &cachedFile = cachedDownload->second;
  std::error_code       filesystemError;
  if (std::filesystem::is_regular_file(cachedFile.filePath, filesystemError) && !filesystemError &&
      calculateDownloadedFileSha256(cachedFile.filePath) == sha256) {
    return cachedFile;
  }

  filesystemError.clear();
  std::filesystem::remove(cachedFile.filePath, filesystemError);
  if (filesystemError) {
    throw std::runtime_error("Could not remove a damaged cached download: " + filesystemError.message());
  }
  m_storedDownloadedFileBytes -= cachedFile.sizeInBytes;
  m_downloadedFilesBySha256.erase(cachedDownload);
  return std::nullopt;
}

void HttpFileDownloader::clearDownloadedFiles() {
  std::error_code filesystemError;
  std::filesystem::remove_all(m_downloadCacheDirectory, filesystemError);
  if (filesystemError) {
    throw std::runtime_error("Could not remove files downloaded by the previous application: " +
                             filesystemError.message());
  }
  m_downloadedFilesBySha256.clear();
  m_storedDownloadedFileBytes = 0U;
  createPrivateCacheDirectory(m_downloadCacheDirectory);
  IOT_LOG_DEBUG(m_logger, "Cleared files downloaded by the previous Python application");
}

} // namespace network
} // namespace iot
