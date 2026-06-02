#include "runtime_config.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace iot {
namespace runtime {
namespace {

TEST(RuntimeConfigTest, UsesTheDocumentedDefaultsWhenNoEnvironmentOverridesExist) {
  tests::ScopedEnvironmentVariable deviceIdEnvironmentVariable("IOT_DEVICE_ID", nullptr);
  tests::ScopedEnvironmentVariable brokerHostEnvironmentVariable("IOT_MQTT_HOST", nullptr);
  tests::ScopedEnvironmentVariable brokerPortEnvironmentVariable("IOT_MQTT_PORT", nullptr);
  char                             programName[]          = "iot_app";
  char                            *commandLineArguments[] = {programName};

  const RuntimeConfig runtimeConfig = loadRuntimeConfig(1, commandLineArguments);

  EXPECT_EQ(runtimeConfig.deviceId, "raspberrypi-01");
  EXPECT_EQ(runtimeConfig.mqttBrokerHost, "127.0.0.1");
  EXPECT_EQ(runtimeConfig.mqttBrokerPort, 1883U);
}

TEST(RuntimeConfigTest, ReadsMqttSettingsFromEnvironmentVariables) {
  tests::ScopedEnvironmentVariable deviceIdEnvironmentVariable("IOT_DEVICE_ID", "test-device");
  tests::ScopedEnvironmentVariable brokerHostEnvironmentVariable("IOT_MQTT_HOST", "mqtt.example.test");
  tests::ScopedEnvironmentVariable brokerPortEnvironmentVariable("IOT_MQTT_PORT", "2883");
  char                             programName[]          = "iot_app";
  char                            *commandLineArguments[] = {programName};

  const RuntimeConfig runtimeConfig = loadRuntimeConfig(1, commandLineArguments);

  EXPECT_EQ(runtimeConfig.deviceId, "test-device");
  EXPECT_EQ(runtimeConfig.mqttBrokerHost, "mqtt.example.test");
  EXPECT_EQ(runtimeConfig.mqttBrokerPort, 2883U);
}

TEST(RuntimeConfigTest, RejectsAnInvalidMqttPort) {
  tests::ScopedEnvironmentVariable brokerPortEnvironmentVariable("IOT_MQTT_PORT", "not-a-port");
  char                             programName[]          = "iot_app";
  char                            *commandLineArguments[] = {programName};

  EXPECT_THROW(loadRuntimeConfig(1, commandLineArguments), std::runtime_error);
}

TEST(RuntimeConfigTest, ReturnsHelpBeforeItNeedsTheDefaultApplicationDirectory) {
  char  programName[]          = "iot_app";
  char  helpArgument[]         = "--help";
  char *commandLineArguments[] = {programName, helpArgument};

  const RuntimeConfig runtimeConfig = loadRuntimeConfig(2, commandLineArguments);

  EXPECT_TRUE(runtimeConfig.showHelp);
  EXPECT_NE(runtimeUsage("/usr/bin/iot_app").find("Usage: iot_app"), std::string::npos);
  EXPECT_NE(runtimeUsage(nullptr).find("Usage: iot_app"), std::string::npos);
}

TEST(RuntimeConfigTest, RejectsMissingArgumentStorageUnknownArgumentsAndPortsOutsideTheValidRange) {
  EXPECT_THROW(loadRuntimeConfig(0, nullptr), std::invalid_argument);

  char  programName[]          = "iot_app";
  char  unknownArgument[]      = "--unknown";
  char *commandLineArguments[] = {programName, unknownArgument};
  EXPECT_THROW(loadRuntimeConfig(2, commandLineArguments), std::runtime_error);

  char *missingArgumentStorage[] = {programName, nullptr};
  EXPECT_THROW(loadRuntimeConfig(2, missingArgumentStorage), std::invalid_argument);

  tests::ScopedEnvironmentVariable zeroBrokerPortEnvironmentVariable("IOT_MQTT_PORT", "0");
  char                            *normalCommandLineArguments[] = {programName};
  EXPECT_THROW(loadRuntimeConfig(1, normalCommandLineArguments), std::runtime_error);
}

TEST(RuntimeConfigTest, RejectsMoreThanOneCommandLineArgument) {
  char  programName[]           = "iot_app";
  char  firstUnknownArgument[]  = "--first";
  char  secondUnknownArgument[] = "--second";
  char *commandLineArguments[]  = {programName, firstUnknownArgument, secondUnknownArgument};

  EXPECT_THROW(loadRuntimeConfig(3, commandLineArguments), std::runtime_error);
}

TEST(RuntimeConfigTest, FindsAnApplicationDirectoryBesideTheRunningExecutable) {
  std::error_code filesystemError;
  const auto      executablePath = std::filesystem::read_symlink("/proc/self/exe", filesystemError);
  ASSERT_FALSE(filesystemError);
  const auto applicationDirectory = executablePath.parent_path() / "default_python_application";
  std::filesystem::create_directories(applicationDirectory, filesystemError);
  ASSERT_FALSE(filesystemError);
  char  programName[]          = "iot_app";
  char *commandLineArguments[] = {programName};

  const RuntimeConfig runtimeConfig = loadRuntimeConfig(1, commandLineArguments);

  EXPECT_EQ(runtimeConfig.defaultApplicationDirectory, applicationDirectory);
}

TEST(RuntimeConfigTest, FindsAnApplicationInTheSameInstallPrefixWhenNoneIsBesideTheExecutable) {
  std::error_code filesystemError;
  const auto      executablePath = std::filesystem::read_symlink("/proc/self/exe", filesystemError);
  ASSERT_FALSE(filesystemError);
  const auto applicationBesideExecutable = executablePath.parent_path() / "default_python_application";
  const auto samePrefixApplication =
      executablePath.parent_path().parent_path() / "share/iot-app/default_python_application";

  std::filesystem::remove_all(applicationBesideExecutable, filesystemError);
  ASSERT_FALSE(filesystemError);
  std::filesystem::create_directories(samePrefixApplication, filesystemError);
  ASSERT_FALSE(filesystemError);

  char                programName[]          = "iot_app";
  char               *commandLineArguments[] = {programName};
  const RuntimeConfig runtimeConfig          = loadRuntimeConfig(1, commandLineArguments);
  EXPECT_EQ(runtimeConfig.defaultApplicationDirectory, samePrefixApplication);

  std::filesystem::remove_all(samePrefixApplication, filesystemError);
  ASSERT_FALSE(filesystemError);
}

} // namespace
} // namespace runtime
} // namespace iot
