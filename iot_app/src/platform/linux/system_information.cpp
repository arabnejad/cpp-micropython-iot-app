#include "iot/system/system_information.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace iot {
namespace system {
namespace {

constexpr const char *unavailableText = "Unavailable";

std::string trim(std::string value) {
  const auto isPadding = [](unsigned char character) { return std::isspace(character) != 0 || character == '\0'; };
  while (!value.empty() && isPadding(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && isPadding(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

/** Reads a small text file and removes trailing newline or null characters. */
std::optional<std::string> readTextFile(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return std::nullopt;
  }
  std::string value;
  std::getline(input, value);
  value = trim(std::move(value));
  if (!value.empty()) {
    return value;
  }
  return std::nullopt;
}

std::string removeMatchingQuotes(std::string value) {
  value = trim(std::move(value));
  if (value.size() >= 2U &&
      ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1U, value.size() - 2U);
  }
  return value;
}

std::string readOperatingSystemName() {
  std::ifstream input{"/etc/os-release"};
  std::string   fallbackName;
  std::string   line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string key   = line.substr(0U, separator);
    const std::string value = removeMatchingQuotes(line.substr(separator + 1U));
    if (key == "PRETTY_NAME") {
      return value;
    }
    if (key == "NAME") {
      fallbackName = value;
    }
  }
  return fallbackName.empty() ? unavailableText : fallbackName;
}

std::string readDeviceModel() {
  for (const std::filesystem::path &path : {std::filesystem::path{"/proc/device-tree/model"},
                                            std::filesystem::path{"/sys/firmware/devicetree/base/model"}}) {
    if (const auto model = readTextFile(path)) {
      return *model;
    }
  }
  return unavailableText;
}

std::optional<std::uint64_t> readUnsignedInteger(const std::filesystem::path &path) {
  const auto text = readTextFile(path);
  if (!text) {
    return std::nullopt;
  }
  // Some drivers use -1 when link speed is unknown. Do not convert that to a
  // large unsigned number.
  if (!text->empty() && text->front() == '-') {
    return std::nullopt;
  }
  try {
    std::size_t parsedCharacters = 0;
    const auto  value            = std::stoull(*text, &parsedCharacters);
    if (parsedCharacters != text->size()) {
      return std::nullopt;
    }
    return value;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::optional<double> readCpuTemperature() {
  const std::filesystem::path thermalDirectory{"/sys/class/thermal"};
  std::error_code             error;
  if (!std::filesystem::exists(thermalDirectory, error)) {
    return std::nullopt;
  }

  std::optional<double> fallbackTemperature;
  for (std::filesystem::directory_iterator entry{thermalDirectory, error}, end; entry != end && !error;
       entry.increment(error)) {
    const std::string name = entry->path().filename().string();
    if (name.rfind("thermal_zone", 0U) != 0U) {
      continue;
    }
    const auto rawTemperature = readUnsignedInteger(entry->path() / "temp");
    if (!rawTemperature) {
      continue;
    }
    const double temperature = static_cast<double>(*rawTemperature) / 1000.0;
    const auto   type        = readTextFile(entry->path() / "type");
    if (type && (*type == "cpu-thermal" || *type == "cpu_thermal" || *type == "soc_thermal")) {
      return temperature;
    }
    if (!fallbackTemperature) {
      fallbackTemperature = temperature;
    }
  }
  return fallbackTemperature;
}

std::uint64_t readAvailableMemoryBytes(std::uint64_t fallbackBytes) {
  std::ifstream input{"/proc/meminfo"};
  std::string   key;
  std::uint64_t valueInKilobytes = 0;
  std::string   unit;
  while (input >> key >> valueInKilobytes >> unit) {
    if (key == "MemAvailable:") {
      return valueInKilobytes * UINT64_C(1024);
    }
  }
  return fallbackBytes;
}

bool hasPrefixFollowedByDigits(const std::string &name, const std::string &prefix) {
  if (name.size() <= prefix.size() || name.compare(0U, prefix.size(), prefix) != 0) {
    return false;
  }
  for (std::size_t index = prefix.size(); index < name.size(); ++index) {
    if (std::isdigit(static_cast<unsigned char>(name[index])) == 0) {
      return false;
    }
  }
  return true;
}

struct LinuxInterfaceCounts {
  std::size_t i2c{0U};
  std::size_t gpio{0U};
  std::size_t spi{0U};
  std::size_t serial{0U};
};

bool isSpiDeviceName(const std::string &name) {
  if (name.rfind("spidev", 0U) != 0U || name.size() <= 6U) {
    return false;
  }
  for (std::size_t index = 6U; index < name.size(); ++index) {
    const char character = name[index];
    if (std::isdigit(static_cast<unsigned char>(character)) == 0 && character != '.') {
      return false;
    }
  }
  return true;
}

LinuxInterfaceCounts countLinuxInterfaces() {
  LinuxInterfaceCounts        counts;
  std::error_code             error;
  const std::filesystem::path deviceDirectory{"/dev"};
  if (!std::filesystem::exists(deviceDirectory, error)) {
    return counts;
  }

  for (std::filesystem::directory_iterator entry{deviceDirectory, error}, end; entry != end && !error;
       entry.increment(error)) {
    const std::string name = entry->path().filename().string();
    if (hasPrefixFollowedByDigits(name, "i2c-")) {
      ++counts.i2c;
    } else if (hasPrefixFollowedByDigits(name, "gpiochip")) {
      ++counts.gpio;
    } else if (isSpiDeviceName(name)) {
      ++counts.spi;
    }

    if (hasPrefixFollowedByDigits(name, "ttyAMA") || hasPrefixFollowedByDigits(name, "ttyUSB") ||
        hasPrefixFollowedByDigits(name, "ttyACM") || hasPrefixFollowedByDigits(name, "ttyS")) {
      ++counts.serial;
    }
  }
  return counts;
}

std::size_t countUsbDevices() {
  std::size_t                 count = 0U;
  std::error_code             error;
  const std::filesystem::path usbDirectory{"/sys/bus/usb/devices"};
  for (std::filesystem::directory_iterator entry{usbDirectory, error}, end; entry != end && !error;
       entry.increment(error)) {
    const std::string name = entry->path().filename().string();
    std::error_code   entryError;
    const bool        hasVendor = std::filesystem::exists(entry->path() / "idVendor", entryError);
    entryError.clear();
    const bool hasProduct = std::filesystem::exists(entry->path() / "idProduct", entryError);
    if (name.rfind("usb", 0U) != 0U && name.find(':') == std::string::npos && hasVendor && hasProduct) {
      ++count;
    }
  }
  return count;
}

std::size_t countInputDevices() {
  std::size_t                 count = 0U;
  std::error_code             error;
  const std::filesystem::path inputDirectory{"/sys/class/input"};
  for (std::filesystem::directory_iterator entry{inputDirectory, error}, end; entry != end && !error;
       entry.increment(error)) {
    if (hasPrefixFollowedByDigits(entry->path().filename().string(), "input")) {
      ++count;
    }
  }
  return count;
}

std::size_t countBlockDevices() {
  std::size_t                 count = 0U;
  std::error_code             error;
  const std::filesystem::path blockDirectory{"/sys/class/block"};
  for (std::filesystem::directory_iterator entry{blockDirectory, error}, end; entry != end && !error;
       entry.increment(error)) {
    const std::string name = entry->path().filename().string();
    if (name.rfind("loop", 0U) == 0U || name.rfind("ram", 0U) == 0U || name.rfind("zram", 0U) == 0U) {
      continue;
    }
    std::error_code entryError;
    if (!std::filesystem::exists(entry->path() / "partition", entryError) && !entryError) {
      ++count;
    }
  }
  return count;
}

std::vector<NetworkInterfaceInformation> readCurrentNetworkInterfaces() {
  std::map<std::string, NetworkInterfaceInformation> interfacesByName;

  // "ifaddrs" means "interface addresses." getifaddrs() gives us a linked
  // list containing the system's network interface names, flags, and
  // addresses. One interface can appear more than once when it has several
  // addresses. The unique_ptr calls freeifaddrs() automatically when this
  // function finishes, including when an exception is raised.
  // Reference: https://man7.org/linux/man-pages/man3/getifaddrs.3.html
  ifaddrs *rawInterfaces = nullptr;
  if (::getifaddrs(&rawInterfaces) != 0) {
    return {};
  }
  const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> networkInterfaceAddresses(rawInterfaces, &freeifaddrs);

  for (const ifaddrs *interfaceAddress = networkInterfaceAddresses.get(); interfaceAddress != nullptr;
       interfaceAddress                = interfaceAddress->ifa_next) {
    if (interfaceAddress->ifa_name == nullptr || interfaceAddress->ifa_addr == nullptr ||
        (interfaceAddress->ifa_flags & IFF_LOOPBACK) != 0U) {
      continue;
    }

    auto &networkInterfaceInformation = interfacesByName[interfaceAddress->ifa_name];
    networkInterfaceInformation.name  = interfaceAddress->ifa_name;
    networkInterfaceInformation.connected =
        networkInterfaceInformation.connected ||
        ((interfaceAddress->ifa_flags & IFF_UP) != 0U && (interfaceAddress->ifa_flags & IFF_RUNNING) != 0U);

    if (interfaceAddress->ifa_addr->sa_family == AF_INET && networkInterfaceInformation.ipv4Address.empty()) {
      std::array<char, INET_ADDRSTRLEN> address{};
      const auto *internetAddress = reinterpret_cast<const sockaddr_in *>(interfaceAddress->ifa_addr);
      if (::inet_ntop(AF_INET, &internetAddress->sin_addr, address.data(), address.size()) != nullptr) {
        networkInterfaceInformation.ipv4Address = address.data();
      }
    }
  }
  std::vector<NetworkInterfaceInformation> networkInterfaces;
  networkInterfaces.reserve(interfacesByName.size());
  for (auto &networkInterfaceEntry : interfacesByName) {
    const auto speed =
        readUnsignedInteger(std::filesystem::path{"/sys/class/net"} / networkInterfaceEntry.first / "speed");
    if (speed && *speed > 0U) {
      networkInterfaceEntry.second.speedMegabitsPerSecond = *speed;
    }
    networkInterfaces.push_back(std::move(networkInterfaceEntry.second));
  }
  return networkInterfaces;
}

} // namespace

SystemInformation LinuxSystemInformationProvider::readSystemInformation() const {
  SystemInformation systemInformation;

  std::array<char, 256> hostname{};
  const int             hostnameResult = ::gethostname(hostname.data(), hostname.size());
  hostname.back()                      = '\0';
  systemInformation.hostname           = hostnameResult == 0 ? hostname.data() : unavailableText;

  utsname kernelInformation{};
  if (::uname(&kernelInformation) == 0) {
    systemInformation.kernelVersion = std::string{"Linux "} + kernelInformation.release;
    systemInformation.architecture  = kernelInformation.machine;
  } else {
    systemInformation.kernelVersion = unavailableText;
    systemInformation.architecture  = unavailableText;
  }

  systemInformation.deviceModel     = readDeviceModel();
  systemInformation.operatingSystem = readOperatingSystemName();

  const long processorCount = ::sysconf(_SC_NPROCESSORS_ONLN);
  if (processorCount > 0 && static_cast<unsigned long>(processorCount) <=
                                static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max())) {
    systemInformation.logicalCpuCount = static_cast<std::uint32_t>(processorCount);
  }
  systemInformation.cpuTemperatureCelsius = readCpuTemperature();

  double loadAverage = 0.0;
  if (::getloadavg(&loadAverage, 1) == 1) {
    systemInformation.oneMinuteLoadAverage = loadAverage;
  }

  struct sysinfo currentSystemInformation {};
  if (::sysinfo(&currentSystemInformation) == 0) {
    const auto memoryUnit = static_cast<std::uint64_t>(currentSystemInformation.mem_unit);
    systemInformation.uptimeSeconds =
        currentSystemInformation.uptime > 0 ? static_cast<std::uint64_t>(currentSystemInformation.uptime) : 0U;
    systemInformation.totalMemoryBytes     = static_cast<std::uint64_t>(currentSystemInformation.totalram) * memoryUnit;
    const auto fallbackFreeBytes           = static_cast<std::uint64_t>(currentSystemInformation.freeram) * memoryUnit;
    systemInformation.availableMemoryBytes = readAvailableMemoryBytes(fallbackFreeBytes);
  }

  struct statvfs rootFileSystem {};
  if (::statvfs("/", &rootFileSystem) == 0) {
    systemInformation.rootStorageTotalBytes =
        static_cast<std::uint64_t>(rootFileSystem.f_blocks) * rootFileSystem.f_frsize;
    systemInformation.rootStorageAvailableBytes =
        static_cast<std::uint64_t>(rootFileSystem.f_bavail) * rootFileSystem.f_frsize;
  }

  const LinuxInterfaceCounts interfaceCounts = countLinuxInterfaces();
  systemInformation.i2cInterfaceCount        = interfaceCounts.i2c;
  systemInformation.gpioControllerCount      = interfaceCounts.gpio;
  systemInformation.spiInterfaceCount        = interfaceCounts.spi;
  systemInformation.serialInterfaceCount     = interfaceCounts.serial;
  systemInformation.usbDeviceCount           = countUsbDevices();
  systemInformation.inputDeviceCount         = countInputDevices();
  systemInformation.blockDeviceCount         = countBlockDevices();
  return systemInformation;
}

std::uint64_t LinuxSystemInformationProvider::readUptimeSeconds() const {
  struct sysinfo currentSystemInformation {};
  if (::sysinfo(&currentSystemInformation) != 0) {
    throw std::system_error(errno, std::generic_category(), "Linux could not read the current system uptime");
  }
  return currentSystemInformation.uptime > 0 ? static_cast<std::uint64_t>(currentSystemInformation.uptime) : 0U;
}

std::vector<NetworkInterfaceInformation> LinuxSystemInformationProvider::readNetworkInterfaces() const {
  return readCurrentNetworkInterfaces();
}

} // namespace system
} // namespace iot
