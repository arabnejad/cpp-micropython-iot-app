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

TEST(LinuxSystemInformationProviderTest, ReadsCurrentTimeUptimeAndAvailableNetworkInterfaces) {
  LinuxSystemInformationProvider linuxSystemInformationProvider;
  const std::string              currentLocalTime = linuxSystemInformationProvider.readCurrentLocalTime();

  ASSERT_EQ(currentLocalTime.size(), 19U);
  EXPECT_EQ(currentLocalTime[4], '-');
  EXPECT_EQ(currentLocalTime[7], '-');
  EXPECT_EQ(currentLocalTime[10], ' ');
  EXPECT_EQ(currentLocalTime[13], ':');
  EXPECT_EQ(currentLocalTime[16], ':');
  EXPECT_GT(linuxSystemInformationProvider.readUptimeSeconds(), 0U);

  for (const NetworkInterfaceInformation &networkInterface : linuxSystemInformationProvider.readNetworkInterfaces()) {
    EXPECT_FALSE(networkInterface.name.empty());
    EXPECT_NE(networkInterface.name, "lo");
  }
}

} // namespace
} // namespace system
} // namespace iot
