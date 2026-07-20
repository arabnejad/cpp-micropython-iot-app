#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace iot {
namespace network {

/* One file requested by a Python application. */
struct FileDownloadRequest {
  std::string url;
  /* Optional SHA-256 text used to verify the downloaded bytes. */
  std::string expectedSha256;
};

/* Information returned after the file has been placed in the download cache. */
struct DownloadedFile {
  std::filesystem::path filePath;
  std::string           sha256;
  std::uint64_t         sizeInBytes{0};
  std::string           contentType;
  bool                  loadedFromCache{false};
};

/* Downloads files for the current MicroPython application. */
class IFileDownloader {
public:
  virtual ~IFileDownloader() = default;

  IFileDownloader(const IFileDownloader &)            = delete;
  IFileDownloader &operator=(const IFileDownloader &) = delete;
  IFileDownloader(IFileDownloader &&)                 = delete;
  IFileDownloader &operator=(IFileDownloader &&)      = delete;

  /* Downloads one URL or returns a cached file with the requested SHA-256. */
  virtual DownloadedFile downloadFile(const FileDownloadRequest &fileDownloadRequest) = 0;

  /* Removes files downloaded by the previous Python application. */
  virtual void clearDownloadedFiles() = 0;

protected:
  IFileDownloader() = default;
};

} // namespace network
} // namespace iot
