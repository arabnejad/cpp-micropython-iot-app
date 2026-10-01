#include "runtime_config.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <filesystem>

#include <unistd.h>

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
  EXPECT_EQ(downloadConnectionTimeout, std::chrono::seconds(10));
  EXPECT_EQ(downloadTotalTimeout, std::chrono::seconds(30));
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

TEST(RuntimePathsTest, CalculatesDownloadAndApplicationPathsUnderOnePerUserRoot) {
  const RuntimePaths runtimePaths = calculateRuntimePathsForCurrentUser();
  const auto         expectedTemporaryRootDirectory =
      std::filesystem::path{"/tmp"} / ("iot-app-" + std::to_string(static_cast<unsigned long>(::getuid())));

  EXPECT_EQ(runtimePaths.temporaryRootDirectory, expectedTemporaryRootDirectory);
  EXPECT_EQ(runtimePaths.downloadedFilesDirectory, expectedTemporaryRootDirectory / "downloads");
  EXPECT_EQ(runtimePaths.receivedApplicationsDirectory, expectedTemporaryRootDirectory / "applications");
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

TEST(RuntimeConfigTest, PrefersAnApplicationDirectoryBesideTheExecutable) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                executableDirectory  = temporaryDirectory.path() / "bin";
  const auto                applicationDirectory = executableDirectory / "default_python_application";
  std::filesystem::create_directories(applicationDirectory);
  std::filesystem::create_directories(temporaryDirectory.path() / "share/iot-app/default_python_application");

  EXPECT_EQ(findDefaultApplicationDirectory(executableDirectory, "share", temporaryDirectory.path() / "fallback"),
            applicationDirectory);
}

TEST(RuntimeConfigTest, FindsAnApplicationInTheSameInstallPrefixWhenNoneIsBesideTheExecutable) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                executableDirectory = temporaryDirectory.path() / "bin";
  const auto samePrefixApplication = temporaryDirectory.path() / "share/iot-app/default_python_application";
  std::filesystem::create_directories(samePrefixApplication);

  EXPECT_EQ(findDefaultApplicationDirectory(executableDirectory, "share", temporaryDirectory.path() / "fallback"),
            samePrefixApplication);
}

TEST(RuntimeConfigTest, FindsAnApplicationInAnAbsoluteDataDirectory) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                dataDirectory        = temporaryDirectory.path() / "custom-data";
  const auto                applicationDirectory = dataDirectory / "iot-app/default_python_application";
  std::filesystem::create_directories(applicationDirectory);

  EXPECT_EQ(findDefaultApplicationDirectory(temporaryDirectory.path() / "bin", dataDirectory,
                                            temporaryDirectory.path() / "fallback"),
            applicationDirectory);
}

TEST(RuntimeConfigTest, ReturnsTheInstalledFallbackWhenNoApplicationDirectoryIsFound) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                fallbackDirectory = temporaryDirectory.path() / "fallback";

  EXPECT_EQ(findDefaultApplicationDirectory(temporaryDirectory.path() / "bin", "share", fallbackDirectory),
            fallbackDirectory);
}

} // namespace
} // namespace runtime
} // namespace iot
