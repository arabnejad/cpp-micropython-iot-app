#include "iot/status/seengreat_status_display.h"

#include "mock_i2c_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FixedSystemInformation final : public iot::system::ISystemInformationProvider {
public:
  explicit FixedSystemInformation(bool connected = true, std::optional<double> temperatureCelsius = 47.6)
      : connected(connected), temperatureCelsius(temperatureCelsius) {}
  iot::system::SystemInformation readSystemInformation() const override {
    if (failSystemRead)
      throw std::runtime_error("system information unavailable");
    iot::system::SystemInformation information;
    information.cpuTemperatureCelsius = temperatureCelsius;
    return information;
  }
  std::string readCurrentLocalTime() const override {
    if (failTimeRead)
      throw std::runtime_error("clock unavailable");
    return "2000-01-01 00:00:00";
  }
  std::uint64_t readUptimeSeconds() const override {
    return 8266;
  }
  std::vector<iot::system::NetworkInterfaceInformation> readNetworkInterfaces() const override {
    if (failNetworkRead)
      throw std::runtime_error("network unavailable");
    return {{"eth0", connected, connected ? "192.168.1.34" : "", {}}};
  }
  bool connected;
  std::optional<double> temperatureCelsius;
  bool failSystemRead{false};
  bool failTimeRead{false};
  bool failNetworkRead{false};
};

bool lit(const std::vector<std::vector<std::uint8_t>> &transfers, unsigned x, unsigned y) {
  return (transfers[1 + 2 * (y / 8)][1 + x] & (1U << (y % 8))) != 0;
}

TEST(SeenGreatStatusDisplayTest, RendersInverseTitleNetworkAppTemperatureAndTimeAsSH1106Pages) {
  iot::tests::MockI2cDevice              device;
  FixedSystemInformation                 info;
  std::vector<std::vector<std::uint8_t>> transfers;
  EXPECT_CALL(device, write(testing::_)).WillRepeatedly([&](const std::vector<std::uint8_t> &bytes) {
    transfers.push_back(bytes);
  });

  iot::status::SeenGreatStatusDisplay display(device);
  ASSERT_EQ(transfers.size(), 1U);
  EXPECT_EQ(transfers[0].front(), 0x00);
  transfers.clear();
  display.show(info, "Dashboard");

  ASSERT_EQ(transfers.size(), 16U); // eight command/data page pairs
  for (unsigned page = 0; page < 8; ++page) {
    EXPECT_EQ(transfers[2 * page],
              (std::vector<std::uint8_t>{0x00, static_cast<std::uint8_t>(0xb0 + page), 0x02, 0x10}));
    ASSERT_EQ(transfers[1 + 2 * page].size(), 129U);
    EXPECT_EQ(transfers[1 + 2 * page][0], 0x40);
  }
  EXPECT_TRUE(lit(transfers, 116, 3)); // network status icon
  EXPECT_TRUE(lit(transfers, 0, 18));  // illuminated inverse title band
  bool hasDarkTitlePixel = false;
  for (unsigned y = 20; y < 36; ++y)
    for (unsigned x = 25; x < 103; ++x)
      hasDarkTitlePixel |= !lit(transfers, x, y);
  EXPECT_TRUE(hasDarkTitlePixel);
  EXPECT_TRUE(lit(transfers, 1, 53)); // divider under app name
  for (unsigned y : {50U, 51U, 52U, 54U, 55U, 56U})
    for (unsigned x = 0; x < 128; ++x)
      EXPECT_FALSE(lit(transfers, x, y));
  EXPECT_TRUE(lit(transfers, 3, 57)); // CPU temperature starts in the left column
  for (unsigned y = 57; y < 64; ++y)
    EXPECT_TRUE(lit(transfers, 64, y)); // divider between temperature and clock
  EXPECT_TRUE(lit(transfers, 80, 57)); // clock starts in the right column
  EXPECT_TRUE(std::any_of(transfers[11].begin() + 1, transfers[11].end(), [](auto byte) { return byte != 0; }));
}

TEST(SeenGreatStatusDisplayTest, ShowsUnavailableTemperatureWithoutChangingTheClock) {
  iot::tests::MockI2cDevice              device;
  FixedSystemInformation                 measured;
  FixedSystemInformation                 unavailable(true, std::nullopt);
  std::vector<std::vector<std::uint8_t>> transfers;
  EXPECT_CALL(device, write(testing::_)).WillRepeatedly([&](const std::vector<std::uint8_t> &bytes) {
    transfers.push_back(bytes);
  });
  iot::status::SeenGreatStatusDisplay display(device);
  transfers.clear();
  display.show(measured, "Dashboard");
  const auto measuredFrame = transfers;

  transfers.clear();
  display.show(unavailable, "Dashboard");
  bool temperatureChanged = false;
  for (unsigned y = 57; y < 64; ++y) {
    for (unsigned x = 0; x < 64; ++x)
      temperatureChanged |= lit(measuredFrame, x, y) != lit(transfers, x, y);
    for (unsigned x = 64; x < 128; ++x)
      EXPECT_EQ(lit(measuredFrame, x, y), lit(transfers, x, y));
  }
  EXPECT_TRUE(temperatureChanged);
}

TEST(SeenGreatStatusDisplayTest, CutsLongAppNamesToOneLineAndUpdatesConnectionStatus) {
  iot::tests::MockI2cDevice              device;
  FixedSystemInformation                 online;
  FixedSystemInformation                 offline(false);
  std::vector<std::vector<std::uint8_t>> transfers;
  EXPECT_CALL(device, write(testing::_)).WillRepeatedly([&](const std::vector<std::uint8_t> &bytes) {
    transfers.push_back(bytes);
  });
  iot::status::SeenGreatStatusDisplay display(device);
  transfers.clear();
  display.show(online, "Temperature Monitoring Dashboard");
  ASSERT_EQ(transfers.size(), 16U);
  const auto longNameFrame = transfers;
  transfers.clear();
  display.show(online, "Temperature MonSomething Else");
  EXPECT_EQ(transfers, longNameFrame); // same first 14 chars, both end in ellipsis
  transfers.clear();
  display.show(offline, "Dashboard");
  ASSERT_EQ(transfers.size(), 16U);
  EXPECT_NE(transfers, longNameFrame);
  EXPECT_FALSE(lit(transfers, 118, 6)); // crossed-out connection icon
}

TEST(SeenGreatStatusDisplayTest, RetriesSystemNetworkAndClockReadsAfterTemporaryFailures) {
  iot::tests::MockI2cDevice              device;
  FixedSystemInformation                 info;
  std::vector<std::vector<std::uint8_t>> transfers;
  EXPECT_CALL(device, write(testing::_)).WillRepeatedly([&](const std::vector<std::uint8_t> &bytes) {
    transfers.push_back(bytes);
  });

  iot::status::SeenGreatStatusDisplay display(device);
  transfers.clear();
  info.failSystemRead  = true;
  info.failNetworkRead = true;
  info.failTimeRead    = true;
  EXPECT_NO_THROW(display.show(info, "Dashboard"));
  ASSERT_EQ(transfers.size(), 16U);
  EXPECT_FALSE(lit(transfers, 118, 6)); // no connection is shown while the read fails
  const auto frameWithPlaceholders = transfers;

  transfers.clear();
  info.failSystemRead  = false;
  info.failNetworkRead = false;
  info.failTimeRead    = false;
  display.show(info, "Dashboard");
  ASSERT_EQ(transfers.size(), 16U);
  EXPECT_NE(transfers, frameWithPlaceholders);
  EXPECT_TRUE(lit(transfers, 116, 3));
}

TEST(SeenGreatStatusDisplayTest, ReportsAnI2cFailureWhileUpdatingTheScreen) {
  iot::tests::MockI2cDevice device;
  FixedSystemInformation    info;
  EXPECT_CALL(device, write(testing::_))
      .WillOnce(testing::Return()) // OLED initialization
      .WillOnce(testing::Throw(std::runtime_error("I2C disconnected")))
      .WillOnce(testing::Return()); // OLED shutdown

  iot::status::SeenGreatStatusDisplay display(device);
  EXPECT_THROW(display.show(info, "Dashboard"), std::runtime_error);
}

TEST(SeenGreatStatusDisplayTest, SendsDisplayOffWhenItIsDestroyed) {
  iot::tests::MockI2cDevice              device;
  std::vector<std::vector<std::uint8_t>> transfers;
  EXPECT_CALL(device, write(testing::_)).WillRepeatedly([&](const std::vector<std::uint8_t> &bytes) {
    transfers.push_back(bytes);
  });
  { iot::status::SeenGreatStatusDisplay display(device); }
  ASSERT_EQ(transfers.size(), 2U);
  EXPECT_EQ(transfers.back(), (std::vector<std::uint8_t>{0x00, 0xae}));
}

TEST(SeenGreatStatusDisplayTest, I2cErrorDuringShutdownDoesNotThrow) {
  iot::tests::MockI2cDevice device;
  EXPECT_CALL(device, write(testing::_))
      .WillOnce(testing::Return())
      .WillOnce(testing::Throw(std::runtime_error("I2C disconnected")));
  { iot::status::SeenGreatStatusDisplay display(device); }
}

} // namespace
