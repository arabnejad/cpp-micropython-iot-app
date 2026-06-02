#include "iot/python/application_metadata.h"

#include <gtest/gtest.h>

namespace iot {
namespace python {
namespace {

TEST(ApplicationMetadataTest, ReadsTheRequiredFieldsFromJson) {
  const ApplicationMetadata applicationMetadata =
      parseApplicationMetadata(R"json({"id":"clock","name":"Clock","entry_point":"main.py"})json");

  EXPECT_EQ(applicationMetadata.applicationId, "clock");
  EXPECT_EQ(applicationMetadata.applicationName, "Clock");
  EXPECT_EQ(applicationMetadata.entryPoint, "main.py");
}

TEST(ApplicationMetadataTest, RejectsAnEntryPointOutsideTheApplicationDirectory) {
  const ApplicationMetadata applicationMetadata{"clock", "Clock", "../main.py"};

  EXPECT_THROW(validateApplicationMetadata(applicationMetadata), std::runtime_error);
}

TEST(ApplicationMetadataTest, RejectsAnApplicationIdWithUnsafeCharacters) {
  const ApplicationMetadata applicationMetadata{"clock/app", "Clock", "main.py"};

  EXPECT_THROW(validateApplicationMetadata(applicationMetadata), std::runtime_error);
}

TEST(ApplicationMetadataTest, RejectsDuplicateRequiredFields) {
  EXPECT_THROW(parseApplicationMetadata(R"json({"id":"one","id":"two","name":"Clock","entry_point":"main.py"})json"),
               std::runtime_error);
}

TEST(ApplicationMetadataTest, RejectsMissingInvalidAndEmptyFields) {
  EXPECT_THROW(parseApplicationMetadata(""), std::runtime_error);
  EXPECT_THROW(parseApplicationMetadata(R"json({"id":"clock","name":"Clock"})json"), std::runtime_error);
  EXPECT_THROW(parseApplicationMetadata(R"json({"id":"clock","name":"","entry_point":"main.py"})json"),
               std::runtime_error);
  EXPECT_THROW(parseApplicationMetadata(R"json({"id":"clock","name":"Clock","entry_point":"main.txt"})json"),
               std::runtime_error);
  EXPECT_THROW(parseApplicationMetadata(R"json({"id":"clock","name":"Clock","entry_point":"/main.py"})json"),
               std::runtime_error);
}

TEST(ApplicationMetadataTest, AcceptsAllSupportedApplicationIdCharacters) {
  const ApplicationMetadata applicationMetadata{"app.Name-01_test", "Test", "nested/main.py"};

  EXPECT_NO_THROW(validateApplicationMetadata(applicationMetadata));
}

} // namespace
} // namespace python
} // namespace iot
