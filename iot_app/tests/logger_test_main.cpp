#include <gtest/gtest.h>

#include <cstdlib>

int main(int argc, char **argv) {
  // This small test executable exists to exercise logger output. CTest hides
  // its output when the tests pass, while the main unit-test executable keeps
  // logging disabled completely.
  static_cast<void>(::setenv("IOT_LOG_LEVEL", "DEBUG", 1));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
