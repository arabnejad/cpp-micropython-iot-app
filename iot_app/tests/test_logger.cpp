#include "iot/logging/logger.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace iot {
namespace logging {
namespace {

TEST(LoggerTest, AcceptsClassAndStandaloneLogMessagesWithBothColourModes) {
  ASSERT_EQ(::setenv("IOT_LOG_COLOR", "always", 1), 0);
  Logger classLogger("TestLogger");
  Logger standaloneLogger;

  EXPECT_NO_THROW(classLogger.debug("test", "A debug message"));
  EXPECT_NO_THROW(classLogger.error("test", "A class log message"));
  EXPECT_NO_THROW(classLogger.warning("test", "A warning message"));
  EXPECT_NO_THROW(classLogger.info(nullptr, "A class log message"));
  EXPECT_NO_THROW(standaloneLogger.info("test", "A standalone log message"));

  ASSERT_EQ(::setenv("IOT_LOG_COLOR", "never", 1), 0);
  EXPECT_NO_THROW(standaloneLogger.info("test", "A log message without colour"));
  ASSERT_EQ(::unsetenv("IOT_LOG_COLOR"), 0);
  EXPECT_NO_THROW(standaloneLogger.info("test", "A log message using terminal detection"));
}

} // namespace
} // namespace logging
} // namespace iot
