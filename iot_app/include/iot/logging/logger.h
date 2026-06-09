#pragma once

#include <sstream>
#include <string>
#include <utility>

namespace iot {
namespace logging {

/* ANSI terminal colours used by the console logger. */
namespace Color {
extern const char reset[];
extern const char red[];
extern const char green[];
extern const char yellow[];
extern const char cyan[];
} // namespace Color

/* Available log levels. */
enum class LogLevel {
  Debug,
  Info,
  Warning,
  Error,
  /* Does not write any log messages. */
  None,
};

/*
 * Writes one-line messages with their level, class, and function names.
 *
 * Give a class logger its class name, for example:
 *
 *   Logger m_logger{"ScreenManager"};
 *
 * A logger used by a standalone function has no class name. The IOT_LOG_*
 * macros add the current function name automatically.
 */
class Logger {
public:
  /* Creates a logger for standalone functions. */
  Logger() = default;

  /* Creates a logger that identifies messages from one class. */
  explicit Logger(std::string className);

  /* Returns true when a message at this level would be written. */
  bool isEnabled(LogLevel level) const noexcept;

  template <typename... MessageParts>
  void debug(const char *functionName, MessageParts &&...messageParts) const noexcept {
    writeMessage(LogLevel::Debug, functionName, std::forward<MessageParts>(messageParts)...);
  }

  template <typename... MessageParts>
  void info(const char *functionName, MessageParts &&...messageParts) const noexcept {
    writeMessage(LogLevel::Info, functionName, std::forward<MessageParts>(messageParts)...);
  }

  template <typename... MessageParts>
  void warning(const char *functionName, MessageParts &&...messageParts) const noexcept {
    writeMessage(LogLevel::Warning, functionName, std::forward<MessageParts>(messageParts)...);
  }

  template <typename... MessageParts>
  void error(const char *functionName, MessageParts &&...messageParts) const noexcept {
    writeMessage(LogLevel::Error, functionName, std::forward<MessageParts>(messageParts)...);
  }

private:
  static void appendMessageParts(std::ostringstream &) {}

  template <typename FirstPart, typename... RemainingParts>
  static void appendMessageParts(std::ostringstream &message, FirstPart &&firstPart,
                                 RemainingParts &&...remainingParts) {
    message << std::forward<FirstPart>(firstPart);
    appendMessageParts(message, std::forward<RemainingParts>(remainingParts)...);
  }

  template <typename... MessageParts>
  void writeMessage(LogLevel level, const char *functionName, MessageParts &&...messageParts) const noexcept {
    if (!isEnabled(level)) {
      return;
    }
    try {
      std::ostringstream message;
      appendMessageParts(message, std::forward<MessageParts>(messageParts)...);
      write(level, functionName, message.str());
    } catch (...) {
      // A failed log message must never stop the application.
    }
  }

  void write(LogLevel level, const char *functionName, const std::string &message) const noexcept;

  std::string m_className;
};

} // namespace logging
} // namespace iot

// __func__ is supplied by C++ and always names the function containing the
// logging call. Keeping it in these macros avoids repeating it at every call.
#define IOT_LOG_DEBUG(logger, ...)                                                                                     \
  do {                                                                                                                 \
    if ((logger).isEnabled(::iot::logging::LogLevel::Debug)) {                                                         \
      (logger).debug(__func__, __VA_ARGS__);                                                                           \
    }                                                                                                                  \
  } while (false)
#define IOT_LOG_INFO(logger, ...)                                                                                      \
  do {                                                                                                                 \
    if ((logger).isEnabled(::iot::logging::LogLevel::Info)) {                                                          \
      (logger).info(__func__, __VA_ARGS__);                                                                            \
    }                                                                                                                  \
  } while (false)
#define IOT_LOG_WARNING(logger, ...)                                                                                   \
  do {                                                                                                                 \
    if ((logger).isEnabled(::iot::logging::LogLevel::Warning)) {                                                       \
      (logger).warning(__func__, __VA_ARGS__);                                                                         \
    }                                                                                                                  \
  } while (false)
#define IOT_LOG_ERROR(logger, ...)                                                                                     \
  do {                                                                                                                 \
    if ((logger).isEnabled(::iot::logging::LogLevel::Error)) {                                                         \
      (logger).error(__func__, __VA_ARGS__);                                                                           \
    }                                                                                                                  \
  } while (false)
