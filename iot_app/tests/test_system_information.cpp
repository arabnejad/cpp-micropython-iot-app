#include "iot/system/system_information.h"

#include <gtest/gtest.h>

namespace iot {
namespace system {
namespace {

TEST(LinuxSystemInformationProviderTest, ReadsBasicInformationFromTheRunningLinuxSystem) {
  LinuxSystemInformationProvider linuxSystemInformationProvider;
  const SystemInformation        systemInformation = linuxSystemInformationProvider.readSystemInformation();

  EXPECT_FALSE(systemInformation.hostname.empty());
  EXPECT_FALSE(systemInformation.kernelVersion.empty());
  EXPECT_GT(systemInformation.logicalCpuCount, 0U);
  EXPECT_GT(systemInformation.totalMemoryBytes, 0U);
  EXPECT_LE(systemInformation.availableMemoryBytes, systemInformation.totalMemoryBytes);
}

TEST(LinuxSystemInformationProviderTest, ReadsUptimeAndAvailableNetworkInterfaces) {
  LinuxSystemInformationProvider linuxSystemInformationProvider;
  EXPECT_GT(linuxSystemInformationProvider.readUptimeSeconds(), 0U);

  for (const NetworkInterfaceInformation &networkInterface : linuxSystemInformationProvider.readNetworkInterfaces()) {
    EXPECT_FALSE(networkInterface.name.empty());
    EXPECT_NE(networkInterface.name, "lo");
  }
}

} // namespace
} // namespace system
} // namespace iot
