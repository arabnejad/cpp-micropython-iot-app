#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iot {
namespace system {

/** Information about one Linux network interface, excluding loopback. */
struct NetworkInterfaceInformation {
  std::string                  name;
  bool                         connected{false};
  std::string                  ipv4Address;
  std::optional<std::uint64_t> speedMegabitsPerSecond;
};

/** System and hardware values collected at one point in time. */
struct SystemInformation {
  std::string hostname;
  std::string deviceModel;
  std::string operatingSystem;
  std::string kernelVersion;
  std::string architecture;

  std::uint64_t         uptimeSeconds{0};
  std::uint32_t         logicalCpuCount{0};
  std::optional<double> cpuTemperatureCelsius;
  std::optional<double> oneMinuteLoadAverage;
  std::uint64_t         totalMemoryBytes{0};
  std::uint64_t         availableMemoryBytes{0};
  std::uint64_t         rootStorageTotalBytes{0};
  std::uint64_t         rootStorageAvailableBytes{0};

  std::size_t i2cInterfaceCount{0};
  std::size_t gpioControllerCount{0};
  std::size_t spiInterfaceCount{0};
  std::size_t serialInterfaceCount{0};
  std::size_t usbDeviceCount{0};
  std::size_t inputDeviceCount{0};
  std::size_t blockDeviceCount{0};
};

/** Common interface used to read system information. */
class ISystemInformationProvider {
public:
  virtual ~ISystemInformationProvider() = default;

  /** Reads a fresh set of system and hardware values. */
  virtual SystemInformation readSystemInformation() const = 0;
  /** Reads only uptime, which is cheaper than rebuilding the full snapshot. */
  virtual std::uint64_t readUptimeSeconds() const = 0;
  /** Reads the current state of every non-loopback network interface. */
  virtual std::vector<NetworkInterfaceInformation> readNetworkInterfaces() const = 0;
};

/** Reads system information from Linux APIs, `/proc`, `/sys`, and `/dev`. */
class LinuxSystemInformationProvider final : public ISystemInformationProvider {
public:
  SystemInformation                        readSystemInformation() const override;
  std::uint64_t                            readUptimeSeconds() const override;
  std::vector<NetworkInterfaceInformation> readNetworkInterfaces() const override;
};

} // namespace system
} // namespace iot
