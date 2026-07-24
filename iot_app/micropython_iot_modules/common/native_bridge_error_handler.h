#pragma once

#include "iot_native_result.h"

#include <array>
#include <cstddef>
#include <exception>

namespace iot::python::internal {

/*
 * Runs C++ work called by a MicroPython C module.
 *
 * A C++ exception cannot cross the C bridge. This helper catches it and
 * returns the message through iot_native_result_t instead. The message is
 * copied into a fixed buffer, so reporting an error does not need to allocate
 * memory. This matters when the original exception was caused by low memory.
 *
 * Each bridge keeps one instance in thread-local storage. The returned error
 * pointer remains valid until that bridge reports another error on the same
 * thread.
 */
class NativeBridgeErrorHandler {
public:
  static constexpr std::size_t errorMessageBufferSizeInBytes = 1024U;

  explicit NativeBridgeErrorHandler(const char *unknownErrorMessage) noexcept
      : m_unknownErrorMessage(unknownErrorMessage) {}

  /*
   * FunctionToRun is the type of the lambda supplied by a bridge function.
   * For example, the network bridge uses it like this:
   *
   *   return nativeBridgeErrorHandler.runSafely([=] {
   *     context().fileDownloader().downloadFile({url, expectedSha256});
   *   });
   *
   * Calling functionToRun() runs the code inside the lambda. The template can
   * receive the lambda directly, so the bridge does not need to create a
   * std::function first.
   */
  template <typename FunctionToRun> iot_native_result_t runSafely(FunctionToRun functionToRun) noexcept {
    try {
      functionToRun();
      return {1, nullptr};
    } catch (const std::exception &error) {
      return failure(error.what());
    } catch (...) {
      return failure(m_unknownErrorMessage);
    }
  }

private:
  iot_native_result_t failure(const char *errorMessage) noexcept {
    const char *messageToCopy = errorMessage;
    if (messageToCopy == nullptr || messageToCopy[0] == '\0') {
      messageToCopy = m_unknownErrorMessage;
    }
    if (messageToCopy == nullptr || messageToCopy[0] == '\0') {
      messageToCopy = "Unknown C++ bridge error";
    }

    std::size_t copiedCharacterCount = 0U;
    while (messageToCopy[copiedCharacterCount] != '\0' && copiedCharacterCount + 1U < m_errorMessage.size()) {
      m_errorMessage[copiedCharacterCount] = messageToCopy[copiedCharacterCount];
      ++copiedCharacterCount;
    }

    if (messageToCopy[copiedCharacterCount] != '\0' && m_errorMessage.size() >= 4U) {
      copiedCharacterCount                      = m_errorMessage.size() - 1U;
      m_errorMessage[copiedCharacterCount - 3U] = '.';
      m_errorMessage[copiedCharacterCount - 2U] = '.';
      m_errorMessage[copiedCharacterCount - 1U] = '.';
    }
    m_errorMessage[copiedCharacterCount] = '\0';

    return {0, m_errorMessage.data()};
  }

  const char                                     *m_unknownErrorMessage;
  std::array<char, errorMessageBufferSizeInBytes> m_errorMessage{};
};

} // namespace iot::python::internal
