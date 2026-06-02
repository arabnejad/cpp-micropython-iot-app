#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace iot {
namespace runtime {

// These are fixed safety and memory limits for this build. They are not user
// configuration, so they live beside RuntimeConfig instead of inside it.
constexpr std::uint16_t mqttKeepAliveSeconds             = 60U;
constexpr std::size_t   maximumPythonSourceSizeInBytes   = 512U * 1024U;
constexpr std::size_t   pythonHeapSizeInBytes            = 1024U * 1024U;
constexpr std::size_t   maximumMqttMessageSizeInBytes    = 1024U * 1024U;
constexpr std::size_t   maximumQueuedApplicationMessages = 4U;
constexpr std::size_t   maximumRememberedDeployments     = 64U;
constexpr std::size_t   maximumPendingRenderCommands     = 256U;

/** Settings the application needs before it can start. */
struct RuntimeConfig {
  bool                  showHelp{false};
  std::filesystem::path defaultApplicationDirectory;

  std::string   deviceId;
  std::string   mqttBrokerHost;
  std::uint16_t mqttBrokerPort{1883U};
  std::string   mqttUsername;
  std::string   mqttPassword;
};

/** Reads startup settings from the command line and environment variables. */
RuntimeConfig loadRuntimeConfig(int argc, char **argv);

/** Builds the short help text shown by `--help`. */
std::string runtimeUsage(const char *programPath);

} // namespace runtime
} // namespace iot
