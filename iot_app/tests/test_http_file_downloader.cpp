#include "iot/network/http_file_downloader.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace iot {
namespace network {
namespace {

/* Serves one fixed HTTP response on the local test machine. */
class OneRequestHttpServer {
public:
  OneRequestHttpServer(std::string responseBody, std::string contentType = "application/octet-stream",
                       std::string               responseStatus = "200 OK",
                       std::chrono::milliseconds responseDelay  = std::chrono::milliseconds{0})
      : m_responseBody(std::move(responseBody)), m_contentType(std::move(contentType)),
        m_responseStatus(std::move(responseStatus)), m_responseDelay(responseDelay) {
    m_listeningSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listeningSocket < 0) {
      throw std::runtime_error("Test could not create its local HTTP socket");
    }

    int reuseAddress = 1;
    ::setsockopt(m_listeningSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));

    sockaddr_in serverAddress{};
    serverAddress.sin_family      = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddress.sin_port        = 0;
    if (::bind(m_listeningSocket, reinterpret_cast<sockaddr *>(&serverAddress), sizeof(serverAddress)) != 0 ||
        ::listen(m_listeningSocket, 1) != 0) {
      ::close(m_listeningSocket);
      throw std::runtime_error("Test could not start its local HTTP server");
    }

    socklen_t serverAddressSize = sizeof(serverAddress);
    if (::getsockname(m_listeningSocket, reinterpret_cast<sockaddr *>(&serverAddress), &serverAddressSize) != 0) {
      ::close(m_listeningSocket);
      throw std::runtime_error("Test could not read its local HTTP port");
    }
    m_port         = ntohs(serverAddress.sin_port);
    m_serverThread = std::thread(&OneRequestHttpServer::serveOneRequest, this);
  }

  ~OneRequestHttpServer() {
    if (m_listeningSocket >= 0) {
      ::shutdown(m_listeningSocket, SHUT_RDWR);
      ::close(m_listeningSocket);
      m_listeningSocket = -1;
    }
    if (m_serverThread.joinable()) {
      m_serverThread.join();
    }
  }

  OneRequestHttpServer(const OneRequestHttpServer &)            = delete;
  OneRequestHttpServer &operator=(const OneRequestHttpServer &) = delete;
  OneRequestHttpServer(OneRequestHttpServer &&)                 = delete;
  OneRequestHttpServer &operator=(OneRequestHttpServer &&)      = delete;

  std::string url(const std::string &path = "/file") const {
    return "http://127.0.0.1:" + std::to_string(m_port) + path;
  }

private:
  void serveOneRequest() {
    const int connectionSocket = ::accept(m_listeningSocket, nullptr, nullptr);
    if (connectionSocket < 0) {
      return;
    }

    std::array<char, 2048U> requestBuffer{};
    static_cast<void>(::recv(connectionSocket, requestBuffer.data(), requestBuffer.size(), 0));
    std::this_thread::sleep_for(m_responseDelay);

    const std::string response = "HTTP/1.1 " + m_responseStatus + "\r\nContent-Type: " + m_contentType +
                                 "\r\nContent-Length: " + std::to_string(m_responseBody.size()) +
                                 "\r\nConnection: close\r\n\r\n" + m_responseBody;
    std::size_t numberOfBytesSent = 0U;
    while (numberOfBytesSent < response.size()) {
      const ssize_t sendResult = ::send(connectionSocket, response.data() + numberOfBytesSent,
                                        response.size() - numberOfBytesSent, MSG_NOSIGNAL);
      if (sendResult <= 0) {
        break;
      }
      numberOfBytesSent += static_cast<std::size_t>(sendResult);
    }
    ::close(connectionSocket);
  }

  int                       m_listeningSocket{-1};
  std::uint16_t             m_port{0};
  std::string               m_responseBody;
  std::string               m_contentType;
  std::string               m_responseStatus;
  std::chrono::milliseconds m_responseDelay;
  std::thread               m_serverThread;
};

HttpFileDownloadLimits testDownloadLimits(std::size_t maximumFileSizeInBytes                  = 1024U,
                                          std::size_t maximumStoredDownloadedFilesSizeInBytes = 4096U) {
  return {maximumFileSizeInBytes, maximumStoredDownloadedFilesSizeInBytes, std::chrono::seconds(2),
          std::chrono::seconds(2)};
}

TEST(HttpFileDownloaderTest, DownloadsAFileChecksItsSha256AndReusesTheCachedCopy) {
  tests::TemporaryDirectory cacheDirectory;
  OneRequestHttpServer      localHttpServer("downloaded bytes", "image/jpeg");
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits());
  const FileDownloadRequest downloadRequest{localHttpServer.url("/picture.jpg"),
                                            "7b13421e5997dd03f43e4cfbbb79dd42eddbe73112928f65cac1f64eca1f96f4"};

  const DownloadedFile firstDownload = fileDownloader.downloadFile(downloadRequest);

  EXPECT_EQ(firstDownload.sizeInBytes, 16U);
  EXPECT_EQ(firstDownload.contentType, "image/jpeg");
  EXPECT_EQ(firstDownload.filePath.filename(), firstDownload.sha256 + ".download");
  EXPECT_FALSE(firstDownload.loadedFromCache);
  EXPECT_TRUE(std::filesystem::is_regular_file(firstDownload.filePath));

  const DownloadedFile secondDownload = fileDownloader.downloadFile(downloadRequest);
  EXPECT_EQ(secondDownload.filePath, firstDownload.filePath);
  EXPECT_TRUE(secondDownload.loadedFromCache);
}

TEST(HttpFileDownloaderTest, ClearsFilesDownloadedByThePreviousPythonApplication) {
  tests::TemporaryDirectory cacheDirectory;
  OneRequestHttpServer      localHttpServer("downloaded bytes", "image/jpeg");
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits());

  const DownloadedFile downloadedFile = fileDownloader.downloadFile({localHttpServer.url("/picture.jpg"), {}});
  ASSERT_TRUE(std::filesystem::exists(downloadedFile.filePath));

  fileDownloader.clearDownloadedFiles();

  EXPECT_TRUE(std::filesystem::is_directory(cacheDirectory.path()));
  EXPECT_TRUE(std::filesystem::is_empty(cacheDirectory.path()));
}

TEST(HttpFileDownloaderTest, LimitsTheTotalSizeOfFilesStoredByOnePythonApplication) {
  tests::TemporaryDirectory cacheDirectory;
  OneRequestHttpServer      firstHttpServer("downloaded bytes");
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U, 20U));
  fileDownloader.downloadFile({firstHttpServer.url("/first.bin"), {}});

  OneRequestHttpServer secondHttpServer("12345678");

  EXPECT_THROW(fileDownloader.downloadFile({secondHttpServer.url("/second.bin"), {}}), std::runtime_error);
}

TEST(HttpFileDownloaderTest, RejectsAFileThatDoesNotMatchTheExpectedSha256) {
  tests::TemporaryDirectory cacheDirectory;
  OneRequestHttpServer      localHttpServer("unexpected data");
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits());

  EXPECT_THROW(fileDownloader.downloadFile({localHttpServer.url(), std::string(64U, '0')}), std::runtime_error);
}

TEST(HttpFileDownloaderTest, StopsWhenTheServerSendsMoreThanTheConfiguredFileSize) {
  tests::TemporaryDirectory cacheDirectory;
  OneRequestHttpServer      localHttpServer(std::string(64U, 'x'));
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U));

  EXPECT_THROW(fileDownloader.downloadFile({localHttpServer.url(), {}}), std::runtime_error);
}

TEST(HttpFileDownloaderTest, ReportsHttpErrorsAndEmptyResponses) {
  tests::TemporaryDirectory firstCacheDirectory;
  OneRequestHttpServer      notFoundServer("not found", "text/plain", "404 Not Found");
  HttpFileDownloader        firstFileDownloader(firstCacheDirectory.path(), testDownloadLimits());
  EXPECT_THROW(firstFileDownloader.downloadFile({notFoundServer.url(), {}}), std::runtime_error);

  tests::TemporaryDirectory secondCacheDirectory;
  OneRequestHttpServer      emptyResponseServer("");
  HttpFileDownloader        secondFileDownloader(secondCacheDirectory.path(), testDownloadLimits());
  EXPECT_THROW(secondFileDownloader.downloadFile({emptyResponseServer.url(), {}}), std::runtime_error);
}

TEST(HttpFileDownloaderTest, ReportsWhenTheCompleteDownloadExceedsItsTimeout) {
  tests::TemporaryDirectory cacheDirectory;
  OneRequestHttpServer      slowHttpServer("late response", "application/octet-stream", "200 OK",
                                           std::chrono::milliseconds(300));
  HttpFileDownloader        fileDownloader(cacheDirectory.path(),
                                           {1024U, 4096U, std::chrono::milliseconds(50), std::chrono::milliseconds(100)});

  const auto downloadStartedAt = std::chrono::steady_clock::now();
  try {
    static_cast<void>(fileDownloader.downloadFile({slowHttpServer.url(), {}}));
    FAIL() << "The download should have timed out";
  } catch (const std::runtime_error &error) {
    EXPECT_NE(std::string(error.what()).find("File download timed out"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("connection limit: 50 ms; transfer limit: 100 ms"), std::string::npos);
  }
  EXPECT_LT(std::chrono::steady_clock::now() - downloadStartedAt, std::chrono::seconds(2));
  EXPECT_TRUE(std::filesystem::is_empty(cacheDirectory.path()));
}

TEST(HttpFileDownloaderTest, RejectsInvalidSettingsUrlsAndSha256Text) {
  tests::TemporaryDirectory cacheDirectory;

  EXPECT_THROW(HttpFileDownloader({}, testDownloadLimits()), std::invalid_argument);
  EXPECT_THROW(HttpFileDownloader(cacheDirectory.path(), {0U, 1U, std::chrono::seconds(1), std::chrono::seconds(1)}),
               std::invalid_argument);
  EXPECT_THROW(HttpFileDownloader(cacheDirectory.path(), {2U, 1U, std::chrono::seconds(1), std::chrono::seconds(1)}),
               std::invalid_argument);
  EXPECT_THROW(HttpFileDownloader(cacheDirectory.path(), {1U, 1U, std::chrono::seconds(0), std::chrono::seconds(1)}),
               std::invalid_argument);
  EXPECT_THROW(HttpFileDownloader(cacheDirectory.path(), {1U, 1U, std::chrono::seconds(1), std::chrono::seconds(0)}),
               std::invalid_argument);

  HttpFileDownloader fileDownloader(cacheDirectory.path(), testDownloadLimits());
  EXPECT_THROW(fileDownloader.downloadFile({{}, {}}), std::invalid_argument);
  EXPECT_THROW(fileDownloader.downloadFile({"file:///tmp/file.jpg", {}}), std::invalid_argument);
  EXPECT_THROW(fileDownloader.downloadFile({"https://example.com/file.jpg", "1234"}), std::invalid_argument);
  EXPECT_THROW(fileDownloader.downloadFile({"https://example.com/file.jpg", std::string(64U, 'z')}),
               std::invalid_argument);
}

TEST(HttpFileDownloaderTest, RejectsACachePathThatIsAFile) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                cacheFilePath = temporaryDirectory.path() / "not-a-directory";
  std::ofstream(cacheFilePath) << "file";

  EXPECT_THROW(HttpFileDownloader(cacheFilePath, testDownloadLimits()), std::runtime_error);
}

TEST(HttpFileDownloaderTest, ReplacesADamagedCachedFileBeforeDownloadingAgain) {
  tests::TemporaryDirectory cacheDirectory;
  const std::string         expectedSha256 = "7b13421e5997dd03f43e4cfbbb79dd42eddbe73112928f65cac1f64eca1f96f4";
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U, 16U));

  DownloadedFile firstDownload;
  {
    OneRequestHttpServer firstHttpServer("downloaded bytes", "image/jpeg");
    firstDownload = fileDownloader.downloadFile({firstHttpServer.url(), expectedSha256});
  }
  std::ofstream(firstDownload.filePath, std::ios::binary | std::ios::trunc) << "damaged";

  OneRequestHttpServer secondHttpServer("downloaded bytes", "image/jpeg");

  const DownloadedFile downloadedFile = fileDownloader.downloadFile({secondHttpServer.url(), expectedSha256});

  EXPECT_FALSE(downloadedFile.loadedFromCache);
  EXPECT_EQ(downloadedFile.sha256, expectedSha256);
  EXPECT_EQ(downloadedFile.sizeInBytes, 16U);
}

TEST(HttpFileDownloaderTest, ReusesAnExistingHashNamedFileAfterASecondDownload) {
  tests::TemporaryDirectory cacheDirectory;
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits());

  DownloadedFile firstDownload;
  {
    OneRequestHttpServer firstHttpServer("same bytes", "application/octet-stream");
    firstDownload = fileDownloader.downloadFile({firstHttpServer.url("/payload.JPEG?token=one"), {}});
  }
  EXPECT_EQ(firstDownload.filePath.filename(), firstDownload.sha256 + ".download");

  OneRequestHttpServer secondHttpServer("same bytes", "application/octet-stream");
  const DownloadedFile secondDownload =
      fileDownloader.downloadFile({secondHttpServer.url("/payload.JPEG?token=two"), {}});

  EXPECT_EQ(secondDownload.filePath, firstDownload.filePath);
  EXPECT_FALSE(secondDownload.loadedFromCache);
}

TEST(HttpFileDownloaderTest, ReplacesACorruptHashNamedFileAtTheFinalCachePath) {
  tests::TemporaryDirectory cacheDirectory;
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits());

  DownloadedFile firstDownload;
  {
    OneRequestHttpServer firstHttpServer("original bytes", "image/jpeg");
    firstDownload = fileDownloader.downloadFile({firstHttpServer.url("/picture"), {}});
  }
  std::ofstream(firstDownload.filePath, std::ios::binary | std::ios::trunc) << "corrupt";

  OneRequestHttpServer secondHttpServer("original bytes", "image/jpeg");
  const DownloadedFile replacementDownload = fileDownloader.downloadFile({secondHttpServer.url("/picture"), {}});

  EXPECT_EQ(replacementDownload.filePath, firstDownload.filePath);
  EXPECT_EQ(replacementDownload.sizeInBytes, 14U);
}

TEST(HttpFileDownloaderTest, FailedDownloadsLeaveNoFilesAndDoNotUseTheStorageAllowance) {
  tests::TemporaryDirectory cacheDirectory;
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U, 16U));
  {
    OneRequestHttpServer oversizedResponse(std::string(32U, 'x'));
    EXPECT_THROW(fileDownloader.downloadFile({oversizedResponse.url(), {}}), std::runtime_error);
  }
  EXPECT_TRUE(std::filesystem::is_empty(cacheDirectory.path()));
  {
    OneRequestHttpServer wrongHashResponse("downloaded bytes");
    EXPECT_THROW(fileDownloader.downloadFile({wrongHashResponse.url(), std::string(64U, '0')}), std::runtime_error);
  }
  EXPECT_TRUE(std::filesystem::is_empty(cacheDirectory.path()));

  OneRequestHttpServer validResponse("downloaded bytes");
  EXPECT_EQ(fileDownloader.downloadFile({validResponse.url(), {}}).sizeInBytes, 16U);
}

TEST(HttpFileDownloaderTest, CountsIdenticalDownloadsOnceAndAllowsHashCacheHitsWhenStorageIsFull) {
  tests::TemporaryDirectory cacheDirectory;
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U, 32U));
  DownloadedFile            firstDownload;
  {
    OneRequestHttpServer firstResponse("downloaded bytes");
    firstDownload = fileDownloader.downloadFile({firstResponse.url(), {}});
  }
  {
    OneRequestHttpServer identicalResponse("downloaded bytes");
    EXPECT_EQ(fileDownloader.downloadFile({identicalResponse.url(), {}}).filePath, firstDownload.filePath);
  }
  {
    OneRequestHttpServer differentResponse(std::string(16U, 'x'));
    EXPECT_EQ(fileDownloader.downloadFile({differentResponse.url(), {}}).sizeInBytes, 16U);
  }

  // No server is needed: a verified cached file can be returned at the limit.
  EXPECT_TRUE(fileDownloader.downloadFile({"http://127.0.0.1:1/file", firstDownload.sha256}).loadedFromCache);
  EXPECT_THROW(fileDownloader.downloadFile({"http://127.0.0.1:1/file", {}}), std::runtime_error);
}

TEST(HttpFileDownloaderTest, ClearingAFullCacheAllowsTheNextApplicationToDownloadAgain) {
  tests::TemporaryDirectory cacheDirectory;
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U, 16U));
  {
    OneRequestHttpServer firstResponse("downloaded bytes");
    fileDownloader.downloadFile({firstResponse.url(), {}});
  }
  fileDownloader.clearDownloadedFiles();
  OneRequestHttpServer nextApplicationResponse(std::string(16U, 'x'));
  EXPECT_EQ(fileDownloader.downloadFile({nextApplicationResponse.url(), {}}).sizeInBytes, 16U);
}

TEST(HttpFileDownloaderTest, FailedCacheInstallationRemovesThePartialFileAndAllowsARetry) {
  tests::TemporaryDirectory cacheDirectory;
  HttpFileDownloader        fileDownloader(cacheDirectory.path(), testDownloadLimits(16U, 16U));
  const auto                blockedCachePath =
      cacheDirectory.path() / "7b13421e5997dd03f43e4cfbbb79dd42eddbe73112928f65cac1f64eca1f96f4.download";
  std::filesystem::create_directory(blockedCachePath);
  std::ofstream(blockedCachePath / "obstacle") << "keep";
  {
    OneRequestHttpServer response("downloaded bytes");
    EXPECT_THROW(fileDownloader.downloadFile({response.url(), {}}), std::runtime_error);
  }
  for (const auto &cacheEntry : std::filesystem::directory_iterator(cacheDirectory.path())) {
    EXPECT_EQ(cacheEntry.path(), blockedCachePath);
  }

  std::filesystem::remove(blockedCachePath / "obstacle");
  std::filesystem::remove(blockedCachePath);
  OneRequestHttpServer retryResponse("downloaded bytes");
  EXPECT_EQ(fileDownloader.downloadFile({retryResponse.url(), {}}).sizeInBytes, 16U);
}

} // namespace
} // namespace network
} // namespace iot
