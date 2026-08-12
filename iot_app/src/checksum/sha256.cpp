#include "checksum/sha256.h"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <memory>

namespace iot {
namespace internal {
namespace {

constexpr std::size_t fileReadBufferSizeInBytes = 16U * 1024U;

using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

DigestContext startSha256Calculation() {
  DigestContext digestContext(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!digestContext || EVP_DigestInit_ex(digestContext.get(), EVP_sha256(), nullptr) != 1) {
    throw Sha256CalculationError(Sha256Failure::CalculationCouldNotStart);
  }
  return digestContext;
}

void addBytesToSha256(EVP_MD_CTX &digestContext, const void *bytes, std::size_t byteCount) {
  if (byteCount > 0U && EVP_DigestUpdate(&digestContext, bytes, byteCount) != 1) {
    throw Sha256CalculationError(Sha256Failure::BytesCouldNotBeProcessed);
  }
}

std::string finishSha256Calculation(EVP_MD_CTX &digestContext) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digestBytes{};
  unsigned int                               digestSize = 0U;
  if (EVP_DigestFinal_ex(&digestContext, digestBytes.data(), &digestSize) != 1) {
    throw Sha256CalculationError(Sha256Failure::CalculationCouldNotFinish);
  }

  static constexpr char hexadecimalDigits[] = "0123456789abcdef";
  std::string           hexadecimalDigest(digestSize * 2U, '0');
  for (unsigned int byteIndex = 0U; byteIndex < digestSize; ++byteIndex) {
    hexadecimalDigest[byteIndex * 2U]      = hexadecimalDigits[digestBytes[byteIndex] >> 4U];
    hexadecimalDigest[byteIndex * 2U + 1U] = hexadecimalDigits[digestBytes[byteIndex] & 0x0FU];
  }
  return hexadecimalDigest;
}

} // namespace

Sha256CalculationError::Sha256CalculationError(Sha256Failure failure)
    : std::runtime_error("SHA-256 calculation failed"), m_failure(failure) {}

Sha256Failure Sha256CalculationError::failure() const noexcept {
  return m_failure;
}

std::string calculateSha256(std::string_view bytes) {
  auto digestContext = startSha256Calculation();
  addBytesToSha256(*digestContext, bytes.data(), bytes.size());
  return finishSha256Calculation(*digestContext);
}

std::string calculateFileSha256(const std::filesystem::path &filePath) {
  std::ifstream inputFile(filePath, std::ios::binary);
  if (!inputFile) {
    throw Sha256CalculationError(Sha256Failure::FileCouldNotBeOpened);
  }

  auto                                        digestContext = startSha256Calculation();
  std::array<char, fileReadBufferSizeInBytes> readBuffer{};
  while (inputFile) {
    inputFile.read(readBuffer.data(), static_cast<std::streamsize>(readBuffer.size()));
    const auto numberOfBytesRead = inputFile.gcount();
    if (numberOfBytesRead > 0) {
      addBytesToSha256(*digestContext, readBuffer.data(), static_cast<std::size_t>(numberOfBytesRead));
    }
  }
  if (!inputFile.eof()) {
    throw Sha256CalculationError(Sha256Failure::FileCouldNotBeRead);
  }
  return finishSha256Calculation(*digestContext);
}

} // namespace internal
} // namespace iot
