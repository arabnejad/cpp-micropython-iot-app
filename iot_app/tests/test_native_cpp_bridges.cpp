#include "display_cpp_bridge.h"
#include "system_cpp_bridge.h"

#include <gtest/gtest.h>

namespace {

TEST(NativeDisplayCppBridgeTest, ReturnsErrorsInsteadOfThrowingAcrossTheCMicroPythonBoundary) {
  iot_text_box_options_t textBoxOptions{};
  std::uint64_t          createdTextBoxId = 0U;

  EXPECT_FALSE(iot_display_clear(0U, 0U, 0U).succeeded);
  EXPECT_FALSE(iot_display_draw_text_box(0, 0, 10, 10, nullptr, &textBoxOptions, &createdTextBoxId).succeeded);
  EXPECT_FALSE(iot_display_update_text_box(0U, "text").succeeded);
  EXPECT_FALSE(iot_display_move_text_box(0U, 0, 0).succeeded);
  EXPECT_FALSE(iot_display_delete_text_box(0U).succeeded);
  EXPECT_FALSE(iot_display_fill_area(0, 0, 10, 10, 0U, 0U, 0U).succeeded);
  EXPECT_FALSE(iot_display_size(nullptr, nullptr).succeeded);
  EXPECT_FALSE(iot_display_monitor_count(nullptr).succeeded);
  EXPECT_FALSE(iot_display_monitor_information(0U, nullptr).succeeded);
  EXPECT_FALSE(iot_display_supported_mode_information(0U, 0U, nullptr).succeeded);
}

TEST(NativeSystemCppBridgeTest, ValidatesOutputPointersAndKeepsTimeAvailableOutsideAnApplication) {
  const char *formattedCurrentTime = nullptr;

  EXPECT_FALSE(iot_system_read_information(nullptr).succeeded);
  EXPECT_TRUE(iot_system_current_time(&formattedCurrentTime).succeeded);
  ASSERT_NE(formattedCurrentTime, nullptr);
  EXPECT_NE(formattedCurrentTime[0], '\0');
  EXPECT_FALSE(iot_system_uptime_seconds(nullptr).succeeded);
  EXPECT_FALSE(iot_system_network_interface_count(nullptr).succeeded);
  EXPECT_FALSE(iot_system_read_network_interface(0U, nullptr).succeeded);
}

} // namespace
