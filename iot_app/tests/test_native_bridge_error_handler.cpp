#include "native_bridge_error_handler.h"

#include <gtest/gtest.h>

#include <cstring>
#include <new>
#include <stdexcept>
#include <string>

namespace {

using iot::python::internal::NativeBridgeErrorHandler;

TEST(NativeBridgeErrorHandlerTest, ReturnsSuccessWhenTheCppOperationFinishes) {
  NativeBridgeErrorHandler errorHandler{"Unknown test error"};
  bool                     operationWasRun = false;

  const iot_native_result_t result = errorHandler.runSafely([&] { operationWasRun = true; });

  EXPECT_TRUE(operationWasRun);
  EXPECT_TRUE(result.succeeded);
  EXPECT_EQ(result.error_message, nullptr);
}

TEST(NativeBridgeErrorHandlerTest, CopiesAStandardCppExceptionMessageIntoTheResult) {
  NativeBridgeErrorHandler errorHandler{"Unknown test error"};

  const iot_native_result_t result = errorHandler.runSafely([] { throw std::runtime_error("Display is unavailable"); });

  EXPECT_FALSE(result.succeeded);
  EXPECT_STREQ(result.error_message, "Display is unavailable");
}

TEST(NativeBridgeErrorHandlerTest, UsesTheModuleMessageForAnUnknownCppException) {
  NativeBridgeErrorHandler errorHandler{"Unknown display bridge error"};

  const iot_native_result_t result = errorHandler.runSafely([] { throw 7; });

  EXPECT_FALSE(result.succeeded);
  EXPECT_STREQ(result.error_message, "Unknown display bridge error");
}

TEST(NativeBridgeErrorHandlerTest, CanReportAnAllocationFailureWithoutCreatingAnotherString) {
  NativeBridgeErrorHandler errorHandler{"Unknown test error"};

  const iot_native_result_t result = errorHandler.runSafely([] { throw std::bad_alloc{}; });

  EXPECT_FALSE(result.succeeded);
  ASSERT_NE(result.error_message, nullptr);
  EXPECT_NE(result.error_message[0], '\0');
}

TEST(NativeBridgeErrorHandlerTest, ShortensAnErrorThatDoesNotFitInTheFixedBuffer) {
  NativeBridgeErrorHandler errorHandler{"Unknown test error"};
  const std::string        longErrorMessage(NativeBridgeErrorHandler::errorMessageBufferSizeInBytes * 2U, 'x');

  const iot_native_result_t result = errorHandler.runSafely([&] { throw std::runtime_error(longErrorMessage); });

  ASSERT_FALSE(result.succeeded);
  ASSERT_NE(result.error_message, nullptr);
  EXPECT_EQ(std::strlen(result.error_message), NativeBridgeErrorHandler::errorMessageBufferSizeInBytes - 1U);
  EXPECT_STREQ(result.error_message + NativeBridgeErrorHandler::errorMessageBufferSizeInBytes - 4U, "...");
}

TEST(NativeBridgeErrorHandlerTest, UsesAGenericMessageWhenNoModuleMessageWasProvided) {
  NativeBridgeErrorHandler errorHandler{nullptr};

  const iot_native_result_t result = errorHandler.runSafely([] { throw 7; });

  EXPECT_FALSE(result.succeeded);
  EXPECT_STREQ(result.error_message, "Unknown C++ bridge error");
}

} // namespace
