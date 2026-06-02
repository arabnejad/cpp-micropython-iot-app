#include <gtest/gtest.h>

#include <cstring>
#include <cstdlib>

int main(int argc, char **argv) {
  // Unit tests deliberately exercise expected error paths. Keep their logger
  // messages out of normal test output. Set IOT_TEST_SHOW_LOGS=ON only when
  // investigating a failing test.
  const char *showLogs = std::getenv("IOT_TEST_SHOW_LOGS");
  if (showLogs == nullptr || std::strcmp(showLogs, "ON") != 0) {
    static_cast<void>(::setenv("IOT_LOG_LEVEL", "NONE", 1));
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
