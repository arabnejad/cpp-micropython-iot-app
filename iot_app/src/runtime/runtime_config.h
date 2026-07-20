#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace iot {
namespace runtime {

// These are fixed safety and memory limits for this build. They are not user
// configuration, so they live beside RuntimeConfig instead of inside it.

constexpr std::uint16_t mqttKeepAliveSeconds             = 60U;           // 60 seconds.
constexpr std::size_t   maximumPythonSourceSizeInBytes   = 512U * 1024U;  // 512 KiB
constexpr std::size_t   pythonHeapSizeInBytes            = 1024U * 1024U; // 1 MiB
constexpr std::size_t   maximumMqttMessageSizeInBytes    = 1024U * 1024U; // 1 MiB
constexpr std::size_t   maximumQueuedApplicationMessages = 4U;            // 4 MQTT application messages.
constexpr std::size_t   maximumRememberedDeployments     = 64U;  // Status records for 64 application deployments.
constexpr std::size_t   maximumPendingRenderCommands     = 256U; // 256 drawing commands waiting for the render thread.
constexpr std::size_t   maximumDownloadedFileSizeInBytes = 10U * 1024U * 1024U;        // 10 MiB
constexpr std::size_t   maximumStoredDownloadedFilesSizeInBytes = 50U * 1024U * 1024U; // 50 MiB per Python app.
constexpr auto          downloadConnectionTimeout               = std::chrono::seconds(10);
constexpr auto          downloadTotalTimeout                    = std::chrono::seconds(30);

/* Values used to find the default application and connect to MQTT. */
struct RuntimeConfig {
  bool                  showHelp{false};
  std::filesystem::path defaultApplicationDirectory;

  std::string   deviceId;
  std::string   mqttBrokerHost;
  std::uint16_t mqttBrokerPort{1883U};
  std::string   mqttUsername;
  std::string   mqttPassword;
};

/*
 * Checks whether --help was requested, finds the installed default Python
 * application, and reads the device ID and MQTT connection values from the
 * environment. The collected values are returned in a RuntimeConfig object.
 */
RuntimeConfig loadRuntimeConfig(int argc, char **argv);

/* Builds the short help text shown by --help. */
std::string runtimeUsage(const char *programPath);

} // namespace runtime
} // namespace iot
