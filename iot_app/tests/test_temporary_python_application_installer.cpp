#include "iot/python/temporary_python_application_installer.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <cjson/cJSON.h>

#include <cstdlib>
#include <fstream>

namespace iot {
namespace python {
namespace {

messaging::ApplicationDeploymentRequest createValidDeploymentRequest() {
  messaging::ApplicationDeploymentRequest deploymentRequest;
  deploymentRequest.transferId      = "transfer-42";
  deploymentRequest.applicationId   = "external-clock";
  deploymentRequest.applicationName = "External clock";
  deploymentRequest.entryPoint      = "main.py";
  deploymentRequest.sourceCode      = "print('clock')\n";
  return deploymentRequest;
}

TEST(TemporaryPythonApplicationInstallerTest, WritesLoadsAndRemovesOneReceivedApplication) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());

  const PythonApplication installedApplication =
      temporaryApplicationInstaller.installAndLoad(createValidDeploymentRequest());

  EXPECT_EQ(installedApplication.applicationId, "external-clock");
  EXPECT_EQ(installedApplication.sourceCode, "print('clock')\n");
  EXPECT_TRUE(std::filesystem::is_regular_file(installedApplication.entryPointPath));

  temporaryApplicationInstaller.removeInstalledApplication(installedApplication.packageDirectory);
  EXPECT_FALSE(std::filesystem::exists(installedApplication.packageDirectory));
}

TEST(TemporaryPythonApplicationInstallerTest, RefusesAnUnsafeTransferIdBeforeWritingFiles) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());
  auto                                deploymentRequest = createValidDeploymentRequest();
  deploymentRequest.transferId                          = "../unsafe";

  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(deploymentRequest), std::runtime_error);
}

TEST(TemporaryPythonApplicationInstallerTest, RequiresANormalNonEmptyTemporaryRootDirectory) {
  const PythonApplicationLoader pythonApplicationLoader(1024U);
  EXPECT_THROW(TemporaryPythonApplicationInstaller(pythonApplicationLoader, {}), std::invalid_argument);

  tests::TemporaryDirectory temporaryDirectory;
  const auto                linkPath = temporaryDirectory.path() / "applications-link";
  std::filesystem::create_directory_symlink(temporaryDirectory.path(), linkPath);
  EXPECT_THROW(TemporaryPythonApplicationInstaller(pythonApplicationLoader, linkPath), std::runtime_error);
}

TEST(TemporaryPythonApplicationInstallerTest, RejectsAnUnsafeEntryPointAndWillNotRemoveOutsideItsRoot) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());
  auto                                deploymentRequest = createValidDeploymentRequest();
  deploymentRequest.entryPoint                          = "../unsafe.py";

  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(deploymentRequest), std::runtime_error);

  const auto outsideDirectory = std::filesystem::path{temporaryDirectory.path().string() + "-outside"};
  std::filesystem::create_directories(outsideDirectory);
  temporaryApplicationInstaller.removeInstalledApplication(outsideDirectory);
  EXPECT_TRUE(std::filesystem::exists(outsideDirectory));
  std::filesystem::remove_all(outsideDirectory);
}

TEST(TemporaryPythonApplicationInstallerTest, CreatesNestedEntryPointDirectoriesAndReplacesTheSameTransfer) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());
  auto                                deploymentRequest = createValidDeploymentRequest();
  deploymentRequest.entryPoint                          = "application/main.py";

  const PythonApplication firstInstalledApplication = temporaryApplicationInstaller.installAndLoad(deploymentRequest);
  EXPECT_TRUE(std::filesystem::is_regular_file(firstInstalledApplication.entryPointPath));
  EXPECT_EQ(firstInstalledApplication.sourceCode, "print('clock')\n");

  deploymentRequest.sourceCode                   = "print('replacement')\n";
  const PythonApplication replacementApplication = temporaryApplicationInstaller.installAndLoad(deploymentRequest);
  EXPECT_EQ(replacementApplication.sourceCode, "print('replacement')\n");
  temporaryApplicationInstaller.removeInstalledApplication(replacementApplication.packageDirectory);
}

TEST(TemporaryPythonApplicationInstallerTest, ClearsOldFilesWhenItTakesOverAnExistingRootDirectory) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                applicationsRoot = temporaryDirectory.path() / "applications";
  std::filesystem::create_directories(applicationsRoot);
  std::ofstream(applicationsRoot / "old-file") << "old";

  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, applicationsRoot);

  EXPECT_FALSE(std::filesystem::exists(applicationsRoot / "old-file"));
}

TEST(TemporaryPythonApplicationInstallerTest, ReportsUnusableRootsAndProvidesTheTemporarySystemRoot) {
  const PythonApplicationLoader pythonApplicationLoader(1024U);
  EXPECT_THROW(TemporaryPythonApplicationInstaller(pythonApplicationLoader, "/proc/iot-app-test-directory"),
               std::runtime_error);

  const std::filesystem::path defaultApplicationsRoot = defaultTemporaryApplicationRoot();
  EXPECT_EQ(defaultApplicationsRoot.filename(), "applications");
  EXPECT_NE(defaultApplicationsRoot.string().find("iot-app-"), std::string::npos);
}

TEST(TemporaryPythonApplicationInstallerTest, ReportsAnErrorWhenItsTemporaryRootIsReplacedWithAFile) {
  tests::TemporaryDirectory           temporaryDirectory;
  const auto                          applicationsRoot = temporaryDirectory.path() / "applications";
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, applicationsRoot);

  std::filesystem::remove_all(applicationsRoot);
  std::ofstream(applicationsRoot) << "not a directory";

  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(createValidDeploymentRequest()), std::runtime_error);
}

TEST(TemporaryPythonApplicationInstallerTest, RemovesTheInstalledCopyWhenTheApplicationLoaderRejectsIt) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(4U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());

  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(createValidDeploymentRequest()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(temporaryDirectory.path() / "transfer-42"));
  EXPECT_FALSE(std::filesystem::exists(temporaryDirectory.path() / ".staging-transfer-42"));
}

TEST(TemporaryPythonApplicationInstallerTest, ReportsAnEntryPointThatWouldOverwriteADirectory) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());
  auto                                deploymentRequest = createValidDeploymentRequest();
  deploymentRequest.entryPoint                          = ".";

  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(deploymentRequest), std::runtime_error);
}

void *alwaysFailJsonAllocation(std::size_t) {
  return nullptr;
}

void ignoreJsonMemoryRelease(void *) {}

std::size_t successfulJsonAllocationsBeforeFailure = 0U;
std::size_t jsonAllocationCount                    = 0U;

void *allocateJsonMemoryUntilConfiguredLimit(std::size_t size) {
  if (jsonAllocationCount++ >= successfulJsonAllocationsBeforeFailure) {
    return nullptr;
  }
  return std::malloc(size);
}

void releaseTestJsonMemory(void *memory) {
  std::free(memory);
}

TEST(TemporaryPythonApplicationInstallerTest, ReportsAnOutOfMemoryErrorWhileCreatingApplicationMetadata) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());
  cJSON_Hooks                         jsonMemoryHooks{&alwaysFailJsonAllocation, &ignoreJsonMemoryRelease};

  cJSON_InitHooks(&jsonMemoryHooks);
  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(createValidDeploymentRequest()), std::runtime_error);
  cJSON_InitHooks(nullptr);
}

TEST(TemporaryPythonApplicationInstallerTest, ReportsAnOutOfMemoryErrorWhileSerializingApplicationMetadata) {
  tests::TemporaryDirectory           temporaryDirectory;
  const PythonApplicationLoader       pythonApplicationLoader(1024U);
  TemporaryPythonApplicationInstaller temporaryApplicationInstaller(pythonApplicationLoader, temporaryDirectory.path());
  cJSON_Hooks                         jsonMemoryHooks{&allocateJsonMemoryUntilConfiguredLimit, &releaseTestJsonMemory};

  successfulJsonAllocationsBeforeFailure = 10U;
  jsonAllocationCount                    = 0U;
  cJSON_InitHooks(&jsonMemoryHooks);
  EXPECT_THROW(temporaryApplicationInstaller.installAndLoad(createValidDeploymentRequest()), std::runtime_error);
  cJSON_InitHooks(nullptr);
}

} // namespace
} // namespace python
} // namespace iot
