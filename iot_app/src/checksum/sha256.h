#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace iot {
namespace internal {

/* Identifies which part of a SHA-256 calculation failed. */
enum class Sha256Failure {
  FileCouldNotBeOpened,
  CalculationCouldNotStart,
  BytesCouldNotBeProcessed,
  FileCouldNotBeRead,
  CalculationCouldNotFinish,
};

/*
 * Lets the caller choose an error message that makes sense for its operation.
 * OpenSSL details remain inside sha256.cpp.
 */
class Sha256CalculationError : public std::runtime_error {
public:
  explicit Sha256CalculationError(Sha256Failure failure);

  Sha256Failure failure() const noexcept;

private:
  Sha256Failure m_failure;
};

/* Returns the lowercase SHA-256 value for the supplied bytes. */
std::string calculateSha256(std::string_view bytes);

/* Reads a file and returns its lowercase SHA-256 value. */
std::string calculateFileSha256(const std::filesystem::path &filePath);

} // namespace internal
} // namespace iot
