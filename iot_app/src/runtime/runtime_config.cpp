#include "runtime_config.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace iot {
namespace runtime {
namespace {

std::filesystem::path executableDirectory(const char *programPath) {
  std::error_code error;
  const auto      executablePath = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error) {
    return executablePath.parent_path();
  }

  const auto fallbackPath = std::filesystem::absolute(programPath, error);
  if (error) {
    throw std::runtime_error("Could not determine the iot_app executable directory");
  }
  return fallbackPath.parent_path();
}

std::filesystem::path findDefaultApplicationDirectory(const char *programPath) {
  const auto executablePathDirectory     = executableDirectory(programPath);
  const auto applicationBesideExecutable = executablePathDirectory / "default_python_application";
  if (std::filesystem::is_directory(applicationBesideExecutable)) {
    return applicationBesideExecutable;
  }

  const std::filesystem::path configuredDataDirectory(IOT_INSTALL_DATA_DIRECTORY);
  const auto                  dataDirectory                  = configuredDataDirectory.is_absolute()
                                                                   ? configuredDataDirectory
                                                                   : executablePathDirectory.parent_path() / configuredDataDirectory;
  const auto                  applicationInSameInstallPrefix = dataDirectory / "iot-app" / "default_python_application";
  if (std::filesystem::is_directory(applicationInSameInstallPrefix)) {
    return applicationInSameInstallPrefix;
  }

  return IOT_INSTALLED_DEFAULT_APPLICATION_DIRECTORY;
}

std::string environmentValueOrDefault(const char *variableName, const char *defaultValue) {
  const char *value = std::getenv(variableName);
  return value == nullptr || value[0] == '\0' ? defaultValue : value;
}

std::uint16_t mqttBrokerPortFromEnvironment() {
  const std::string configuredPort   = environmentValueOrDefault("IOT_MQTT_PORT", "1883");
  std::size_t       parsedCharacters = 0U;
  unsigned long     port             = 0UL;
  try {
    port = std::stoul(configuredPort, &parsedCharacters, 10);
  } catch (const std::exception &) {
    throw std::runtime_error("IOT_MQTT_PORT must contain a port number between 1 and 65535");
  }
  if (parsedCharacters != configuredPort.size() || port == 0UL || port > 65535UL) {
    throw std::runtime_error("IOT_MQTT_PORT must contain a port number between 1 and 65535");
  }
  return static_cast<std::uint16_t>(port);
}

} // namespace

RuntimeConfig loadRuntimeConfig(int argc, char **argv) {
  if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
    throw std::invalid_argument("Runtime command-line arguments are not available");
  }

  RuntimeConfig runtimeConfig;
  if (argc == 2) {
    if (argv[1] == nullptr) {
      throw std::invalid_argument("Runtime command-line argument is not available");
    }
    const std::string argument(argv[1]);
    if (argument == "--help" || argument == "-h") {
      runtimeConfig.showHelp = true;
      return runtimeConfig;
    }
    throw std::runtime_error("Unknown argument '" + argument + "'\n" + runtimeUsage(argv[0]));
  }
  if (argc > 2) {
    throw std::runtime_error("Only one command-line argument is supported\n" + runtimeUsage(argv[0]));
  }

  runtimeConfig.defaultApplicationDirectory = findDefaultApplicationDirectory(argv[0]);
  runtimeConfig.deviceId                    = environmentValueOrDefault("IOT_DEVICE_ID", "raspberrypi-01");
  runtimeConfig.mqttBrokerHost              = environmentValueOrDefault("IOT_MQTT_HOST", "127.0.0.1");
  runtimeConfig.mqttBrokerPort              = mqttBrokerPortFromEnvironment();
  runtimeConfig.mqttUsername                = environmentValueOrDefault("IOT_MQTT_USERNAME", "");
  runtimeConfig.mqttPassword                = environmentValueOrDefault("IOT_MQTT_PASSWORD", "");

  if (runtimeConfig.defaultApplicationDirectory.empty()) {
    throw std::runtime_error("The shipped default application directory could not be determined");
  }
  return runtimeConfig;
}

std::string runtimeUsage(const char *programPath) {
  const std::string programName = programPath == nullptr ? "iot_app" : std::filesystem::path(programPath).filename();
  return "Usage: " + programName +
         " [--help]\n"
         "MQTT settings: IOT_DEVICE_ID, IOT_MQTT_HOST, IOT_MQTT_PORT, IOT_MQTT_USERNAME, IOT_MQTT_PASSWORD";
}

} // namespace runtime
} // namespace iot
