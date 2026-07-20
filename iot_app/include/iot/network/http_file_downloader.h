#pragma once

#include "iot/logging/logger.h"
#include "iot/network/ifile_downloader.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace iot {
namespace network {

/* Limits for one transfer and for downloaded files kept by the current app. */
struct HttpFileDownloadLimits {
  std::size_t               maximumFileSizeInBytes{0};
  std::size_t               maximumStoredDownloadedFilesSizeInBytes{0};
  std::chrono::milliseconds connectionTimeout{0};
  std::chrono::milliseconds totalTimeout{0};
};

/* Downloads files with libcurl and stores them in a private temporary cache. */
class HttpFileDownloader final : public IFileDownloader {
public:
  HttpFileDownloader(std::filesystem::path downloadCacheDirectory, HttpFileDownloadLimits downloadLimits);
  ~HttpFileDownloader() override;

  HttpFileDownloader(const HttpFileDownloader &)            = delete;
  HttpFileDownloader &operator=(const HttpFileDownloader &) = delete;
  HttpFileDownloader(HttpFileDownloader &&)                 = delete;
  HttpFileDownloader &operator=(HttpFileDownloader &&)      = delete;

  DownloadedFile downloadFile(const FileDownloadRequest &fileDownloadRequest) override;
  void           clearDownloadedFiles() override;

private:
  /* Returns a checked cached file, or removes its record if the file is damaged. */
  std::optional<DownloadedFile> findValidCachedDownload(const std::string &sha256);

  logging::Logger                                 m_logger{"HttpFileDownloader"};
  std::filesystem::path                           m_downloadCacheDirectory;
  HttpFileDownloadLimits                          m_downloadLimits;
  std::unordered_map<std::string, DownloadedFile> m_downloadedFilesBySha256;
  std::size_t                                     m_storedDownloadedFileBytes{0};
};

} // namespace network
} // namespace iot
