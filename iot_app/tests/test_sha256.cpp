#include "checksum/sha256.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace iot {
namespace internal {
namespace {

TEST(Sha256Test, CalculatesKnownValuesForEmptyTextAndBinaryBytes) {
  EXPECT_EQ(calculateSha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(calculateSha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  const std::string bytesContainingANullCharacter{"a\0b", 3U};
  EXPECT_EQ(calculateSha256(bytesContainingANullCharacter),
            "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138");
}

TEST(Sha256Test, ReadsEveryByteFromAFileLargerThanItsReadBuffer) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                filePath = temporaryDirectory.path() / "large-file.bin";
  const std::string         fileContents(20U * 1024U, 'x');
  {
    std::ofstream outputFile(filePath, std::ios::binary);
    ASSERT_TRUE(outputFile);
    outputFile.write(fileContents.data(), static_cast<std::streamsize>(fileContents.size()));
    ASSERT_TRUE(outputFile);
  }

  EXPECT_EQ(calculateFileSha256(filePath), calculateSha256(fileContents));
}

TEST(Sha256Test, ReportsWhenTheFileCannotBeOpened) {
  tests::TemporaryDirectory temporaryDirectory;
  const auto                missingFilePath = temporaryDirectory.path() / "missing-file.bin";

  try {
    static_cast<void>(calculateFileSha256(missingFilePath));
    FAIL() << "Expected the missing file to be rejected";
  } catch (const Sha256CalculationError &error) {
    EXPECT_EQ(error.failure(), Sha256Failure::FileCouldNotBeOpened);
  }
}

} // namespace
} // namespace internal
} // namespace iot
