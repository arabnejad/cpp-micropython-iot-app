#include "display_cpp_bridge.h"
#include "network_cpp_bridge.h"
#include "system_cpp_bridge.h"

#include "iot/python/micropython_application_context.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace {

TEST(NativeDisplayCppBridgeTest, ReturnsErrorsInsteadOfThrowingAcrossTheCMicroPythonBoundary) {
  iot_text_box_options_t textBoxOptions{};
  std::uint64_t          createdTextBoxId = 0U;

  EXPECT_FALSE(iot_display_clear(0U, 0U, 0U).succeeded);
  EXPECT_FALSE(iot_display_draw_text_box(0, 0, 10, 10, nullptr, &textBoxOptions, &createdTextBoxId).succeeded);
  EXPECT_FALSE(iot_display_update_text_box(0U, "text").succeeded);
  EXPECT_FALSE(iot_display_move_text_box(0U, 0, 0).succeeded);
  EXPECT_FALSE(iot_display_delete_text_box(0U).succeeded);
  EXPECT_FALSE(iot_display_draw_image(nullptr, 0, 0, 100U, &createdTextBoxId).succeeded);
  EXPECT_FALSE(iot_display_update_image(0U, nullptr).succeeded);
  EXPECT_FALSE(iot_display_move_image(0U, 0, 0).succeeded);
  EXPECT_FALSE(iot_display_set_image_scale(0U, 100U).succeeded);
  EXPECT_FALSE(iot_display_delete_image(0U).succeeded);
  EXPECT_FALSE(iot_display_set_background_image(nullptr, IOT_BACKGROUND_IMAGE_CENTER, 100U).succeeded);
  EXPECT_FALSE(iot_display_clear_background_image().succeeded);
  EXPECT_FALSE(iot_display_fill_area(0, 0, 10, 10, 0U, 0U, 0U).succeeded);
  EXPECT_FALSE(iot_display_size(nullptr, nullptr).succeeded);
  EXPECT_FALSE(iot_display_monitor_count(nullptr).succeeded);
  EXPECT_FALSE(iot_display_monitor_information(0U, nullptr).succeeded);
  EXPECT_FALSE(iot_display_supported_mode_information(0U, 0U, nullptr).succeeded);
}

TEST(NativeNetworkCppBridgeTest, ReturnsErrorsInsteadOfThrowingAcrossTheCMicroPythonBoundary) {
  iot_downloaded_file_t downloadedFile{};
  EXPECT_FALSE(iot_network_download_file(nullptr, "", &downloadedFile).succeeded);
  EXPECT_FALSE(iot_network_download_file("https://example.com/image.jpg", "", &downloadedFile).succeeded);
}

TEST(NativeSystemCppBridgeTest, RejectsSystemCallsWhenNoApplicationContextIsActive) {
  const char *formattedCurrentTime = nullptr;

  EXPECT_FALSE(iot_system_read_information(nullptr).succeeded);
  EXPECT_FALSE(iot_system_current_time(&formattedCurrentTime).succeeded);
  EXPECT_FALSE(iot_system_uptime_seconds(nullptr).succeeded);
  EXPECT_FALSE(iot_system_network_interface_count(nullptr).succeeded);
  EXPECT_FALSE(iot_system_read_network_interface(0U, nullptr).succeeded);
}

class NativeSystemCppBridgeWithContextTest : public ::testing::Test {
protected:
  NativeSystemCppBridgeWithContextTest()
      : m_screenManager(iot::tests::testActiveDisplay(), std::make_unique<iot::tests::RecordingRenderBackend>(), 4U),
        m_connectedDisplays(iot::tests::testConnectedDisplays()),
        m_applicationContext(m_screenManager, iot::tests::testActiveDisplay(), m_connectedDisplays,
                             m_systemInformationProvider, m_fileDownloader,
                             m_systemInformationProvider.readSystemInformation(), "System bridge test") {}

  iot::ui::ScreenManager                     m_screenManager;
  std::vector<iot::display::DisplayInfo>     m_connectedDisplays;
  iot::tests::TestSystemInformationProvider  m_systemInformationProvider;
  iot::tests::TestFileDownloader             m_fileDownloader;
  iot::python::MicroPythonApplicationContext m_applicationContext;
};

TEST_F(NativeSystemCppBridgeWithContextTest, ReturnsTheCurrentTimeReadByTheSystemInformationProvider) {
  const char *formattedCurrentTime = nullptr;

  const iot_native_result_t result = iot_system_current_time(&formattedCurrentTime);

  ASSERT_TRUE(result.succeeded) << result.error_message;
  ASSERT_NE(formattedCurrentTime, nullptr);
  EXPECT_STREQ(formattedCurrentTime, "2000-01-01 00:00:00");
}

TEST_F(NativeSystemCppBridgeWithContextTest, ReportsAnErrorFromTheSystemInformationProvider) {
  m_systemInformationProvider.failWhenReadingCurrentLocalTime = true;
  const char *formattedCurrentTime                            = nullptr;

  const iot_native_result_t result = iot_system_current_time(&formattedCurrentTime);

  EXPECT_FALSE(result.succeeded);
  ASSERT_NE(result.error_message, nullptr);
  EXPECT_STREQ(result.error_message, "test system clock failed");
  EXPECT_EQ(formattedCurrentTime, nullptr);
}

} // namespace
