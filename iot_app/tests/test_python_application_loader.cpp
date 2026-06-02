#include "iot/python/python_application_loader.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <fstream>

namespace iot {
namespace python {
namespace {

void writeTestFile(const std::filesystem::path &filePath, const std::string &fileContents) {
  std::ofstream outputFile(filePath);
  outputFile << fileContents;
}

TEST(PythonApplicationLoaderTest, LoadsMetadataAndPythonSourceFromOneDirectory) {
  tests::TemporaryDirectory temporaryDirectory;
  writeTestFile(temporaryDirectory.path() / "app.json",
                R"json({"id":"clock","name":"Clock","entry_point":"main.py"})json");
  writeTestFile(temporaryDirectory.path() / "main.py", "print('clock')\n");
  const PythonApplicationLoader pythonApplicationLoader(1024U);

  const PythonApplication loadedApplication = pythonApplicationLoader.load(temporaryDirectory.path());

  EXPECT_EQ(loadedApplication.applicationId, "clock");
  EXPECT_EQ(loadedApplication.applicationName, "Clock");
  EXPECT_EQ(loadedApplication.sourceCode, "print('clock')\n");
}

TEST(PythonApplicationLoaderTest, RejectsAnEntryPointThatResolvesOutsideThePackage) {
  tests::TemporaryDirectory temporaryDirectory;
  writeTestFile(temporaryDirectory.path() / "app.json",
                R"json({"id":"clock","name":"Clock","entry_point":"../outside.py"})json");
  writeTestFile(temporaryDirectory.path().parent_path() / "outside.py", "print('outside')\n");
  const PythonApplicationLoader pythonApplicationLoader(1024U);

  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path()), std::runtime_error);
}

TEST(PythonApplicationLoaderTest, RejectsSourceLargerThanItsConfiguredLimit) {
  tests::TemporaryDirectory temporaryDirectory;
  writeTestFile(temporaryDirectory.path() / "app.json",
                R"json({"id":"clock","name":"Clock","entry_point":"main.py"})json");
  writeTestFile(temporaryDirectory.path() / "main.py", "this source is deliberately too long");
  const PythonApplicationLoader pythonApplicationLoader(8U);

  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path()), std::runtime_error);
}

TEST(PythonApplicationLoaderTest, RequiresANonZeroSourceSizeLimit) {
  EXPECT_THROW(PythonApplicationLoader(0U), std::invalid_argument);
}

TEST(PythonApplicationLoaderTest, RejectsAMissingApplicationDirectoryAndMetadataFile) {
  tests::TemporaryDirectory     temporaryDirectory;
  const PythonApplicationLoader pythonApplicationLoader(1024U);

  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path() / "missing"), std::runtime_error);
  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path()), std::runtime_error);
}

TEST(PythonApplicationLoaderTest, RejectsAnEmptyMetadataFileAndAMissingEntryPoint) {
  tests::TemporaryDirectory     temporaryDirectory;
  const PythonApplicationLoader pythonApplicationLoader(1024U);

  writeTestFile(temporaryDirectory.path() / "app.json", "");
  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path()), std::runtime_error);

  writeTestFile(temporaryDirectory.path() / "app.json",
                R"json({"id":"clock","name":"Clock","entry_point":"missing.py"})json");
  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path()), std::runtime_error);
}

TEST(PythonApplicationLoaderTest, RejectsPythonSourceContainingANullByte) {
  tests::TemporaryDirectory temporaryDirectory;
  writeTestFile(temporaryDirectory.path() / "app.json",
                R"json({"id":"clock","name":"Clock","entry_point":"main.py"})json");
  const std::string sourceWithNullByte{"print('clock')\0", 15U};
  writeTestFile(temporaryDirectory.path() / "main.py", sourceWithNullByte);
  const PythonApplicationLoader pythonApplicationLoader(1024U);

  EXPECT_THROW(pythonApplicationLoader.load(temporaryDirectory.path()), std::runtime_error);
}

} // namespace
} // namespace python
} // namespace iot
