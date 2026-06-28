#include "iot/display/display_manager.h"

#include "platform/linux/internal/idrm_display_api.h"

#include "test_support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace iot {
namespace display {
namespace {

class MockDrmDisplayApi : public internal::IDrmDisplayApi {
public:
  MOCK_METHOD(std::vector<DisplayInfo>, connectedDisplays, (), (const, override));
};

TEST(ActiveDisplayTest, KeepsTheSelectedDisplayAndItsActiveModeTogether) {
  const ActiveDisplay activeDisplay = tests::testActiveDisplay();

  EXPECT_EQ(activeDisplay.display().displayId.connectorName, "HDMI-A-1");
  EXPECT_EQ(activeDisplay.display().model, "Test monitor");
  EXPECT_EQ(activeDisplay.mode().name, "1920x1080");
  EXPECT_EQ(activeDisplay.mode().width, 1920U);
  EXPECT_EQ(activeDisplay.mode().height, 1080U);
}

TEST(DisplayTypesTest, ComparesEveryPartOfADisplayIdentity) {
  const DisplayId original{"/dev/dri/card0", "HDMI-A-1", 35U};

  EXPECT_EQ(original, (DisplayId{"/dev/dri/card0", "HDMI-A-1", 35U}));
  EXPECT_FALSE(original == (DisplayId{"/dev/dri/card1", "HDMI-A-1", 35U}));
  EXPECT_FALSE(original == (DisplayId{"/dev/dri/card0", "HDMI-A-2", 35U}));
  EXPECT_FALSE(original == (DisplayId{"/dev/dri/card0", "HDMI-A-1", 44U}));
}

TEST(DisplayManagerTest, ReturnsTheDisplaysReportedByLinuxDrm) {
  MockDrmDisplayApi drmDisplayApi;
  DisplayManager    displayManager(drmDisplayApi);
  DisplayInfo       connectedDisplay = tests::testActiveDisplay().display();
  EXPECT_CALL(drmDisplayApi, connectedDisplays())
      .WillOnce(::testing::Return(std::vector<DisplayInfo>{connectedDisplay}));

  const auto connectedDisplays = displayManager.connectedDisplays();

  ASSERT_EQ(connectedDisplays.size(), 1U);
  EXPECT_EQ(connectedDisplays.front().displayId, connectedDisplay.displayId);
}

TEST(DisplayManagerTest, ReadsTheActiveModeForTheRequestedDisplay) {
  MockDrmDisplayApi   drmDisplayApi;
  DisplayManager      displayManager(drmDisplayApi);
  const ActiveDisplay expectedActiveDisplay = tests::testActiveDisplay();
  DisplayInfo         connectedDisplay      = expectedActiveDisplay.display();
  connectedDisplay.currentMode              = expectedActiveDisplay.mode();
  EXPECT_CALL(drmDisplayApi, connectedDisplays())
      .WillOnce(::testing::Return(std::vector<DisplayInfo>{connectedDisplay}));

  const ActiveDisplay activeDisplay = displayManager.readActiveDisplay(connectedDisplay.displayId);

  EXPECT_EQ(activeDisplay.display().displayId, connectedDisplay.displayId);
  EXPECT_EQ(activeDisplay.mode().width, 1920U);
  EXPECT_EQ(activeDisplay.mode().height, 1080U);
}

TEST(DisplayManagerTest, ReportsAConnectedDisplayWithoutAnActiveMode) {
  MockDrmDisplayApi drmDisplayApi;
  DisplayManager    displayManager(drmDisplayApi);
  DisplayInfo       connectedDisplay = tests::testActiveDisplay().display();
  connectedDisplay.currentMode.reset();
  EXPECT_CALL(drmDisplayApi, connectedDisplays())
      .WillOnce(::testing::Return(std::vector<DisplayInfo>{connectedDisplay}));

  EXPECT_THROW(displayManager.readActiveDisplay(connectedDisplay.displayId), std::runtime_error);
}

TEST(DisplayManagerTest, ReportsWhenTheRequestedDisplayIsNotConnected) {
  MockDrmDisplayApi drmDisplayApi;
  DisplayManager    displayManager(drmDisplayApi);
  EXPECT_CALL(drmDisplayApi, connectedDisplays()).WillOnce(::testing::Return(std::vector<DisplayInfo>{}));

  EXPECT_THROW(displayManager.readActiveDisplay({"/dev/dri/card0", "HDMI-A-1", 35U}), std::runtime_error);
}

TEST(DisplayManagerIntegrationTest, CanSafelyScanAHostWithoutADrmMonitor) {
  DisplayManager displayManager;
  EXPECT_NO_THROW(static_cast<void>(displayManager.connectedDisplays()));
}

} // namespace
} // namespace display
} // namespace iot
