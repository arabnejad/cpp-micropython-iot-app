#include "iot/logging/logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

#include <unistd.h>

namespace iot {
namespace logging {

namespace Color {
const char reset[]  = "\033[0m";
const char red[]    = "\033[31m";
const char green[]  = "\033[32m";
const char yellow[] = "\033[33m";
const char cyan[]   = "\033[36m";
} // namespace Color

namespace {

constexpr std::size_t levelColumnWidth   = 12U;
constexpr const char  projectLogPrefix[] = "[IOT_APP]";

std::mutex &outputMutex() {
  static std::mutex mutex;
  return mutex;
}

const char *levelName(LogLevel level) noexcept {
  switch (level) {
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warning:
    return "WARNING";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::None:
    return "NONE";
  }
  return "UNKNOWN";
}

const char *levelColor(LogLevel level) noexcept {
  switch (level) {
  case LogLevel::Debug:
    return Color::cyan;
  case LogLevel::Info:
    return Color::green;
  case LogLevel::Warning:
    return Color::yellow;
  case LogLevel::Error:
    return Color::red;
  case LogLevel::None:
    return Color::reset;
  }
  return Color::reset;
}

LogLevel configuredMinimumLevel() noexcept {
  try {
    const char *environmentValue = std::getenv("IOT_LOG_LEVEL");
    if (environmentValue == nullptr) {
      return LogLevel::Info;
    }

    std::string level(environmentValue);
    std::transform(level.begin(), level.end(), level.begin(),
                   [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    if (level == "DEBUG") {
      return LogLevel::Debug;
    }
    if (level == "WARNING" || level == "WARN") {
      return LogLevel::Warning;
    }
    if (level == "ERROR") {
      return LogLevel::Error;
    }
    if (level == "NONE" || level == "OFF") {
      return LogLevel::None;
    }
  } catch (...) {
    // Fall back to normal information logging if configuration cannot be read.
  }
  return LogLevel::Info;
}

bool terminalSupportsColor(std::FILE *output) noexcept {
  const char *colorMode = std::getenv("IOT_LOG_COLOR");
  if (colorMode != nullptr && std::string(colorMode) == "always") {
    return true;
  }
  if (colorMode != nullptr && std::string(colorMode) == "never") {
    return false;
  }
  const char *noColor = std::getenv("NO_COLOR");
  return (noColor == nullptr || noColor[0] == '\0') && ::isatty(::fileno(output)) != 0;
}

} // namespace

Logger::Logger(std::string className) : m_className(std::move(className)) {}

bool Logger::isEnabled(LogLevel level) const noexcept {
  static const LogLevel minimumLevel = configuredMinimumLevel();
  return static_cast<int>(level) >= static_cast<int>(minimumLevel);
}

void Logger::write(LogLevel level, const char *functionName, const std::string &message) const noexcept {
  try {
    const std::string levelColumn = '[' + std::string(levelName(level)) + ']';
    std::string       line        = projectLogPrefix;
    line += levelColumn;
    if (levelColumn.size() < levelColumnWidth) {
      line.append(levelColumnWidth - levelColumn.size(), ' ');
    }
    if (!m_className.empty()) {
      line += '[' + m_className + ']';
    }
    line += '[' + std::string(functionName == nullptr ? "unknown" : functionName) + ']';
    line += ' ';
    line += message;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      line.pop_back();
    }

    std::lock_guard<std::mutex> lock(outputMutex());
    std::FILE                  *output = level == LogLevel::Error || level == LogLevel::Warning ? stderr : stdout;
    if (terminalSupportsColor(output)) {
      line = std::string(levelColor(level)) + line + Color::reset;
    }
    line.push_back('\n');
    static_cast<void>(std::fwrite(line.data(), sizeof(char), line.size(), output));
    static_cast<void>(std::fflush(output));
  } catch (...) {
    // Logging must never stop the application while it is reporting another
    // problem.
  }
}

} // namespace logging
} // namespace iot
