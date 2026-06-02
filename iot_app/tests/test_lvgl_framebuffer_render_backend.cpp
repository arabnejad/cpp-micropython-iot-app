#include "iot/ui/ilvgl_framebuffer_driver.h"
#include "iot/ui/render_backend.h"

#include "test_support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lvgl.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace iot {
namespace ui {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

class MockLvglFramebufferDriver : public ILvglFramebufferDriver {
public:
  MOCK_METHOD(lv_display_t *, createDisplay, (), (override));
  MOCK_METHOD(bool, openFramebuffer, (lv_display_t * lvglDisplay, const char *framebufferDevicePath), (override));
};

display::ActiveDisplay createActiveDisplayWithResolution(std::uint32_t width, std::uint32_t height) {
  auto activeDisplay = tests::testActiveDisplay();
  auto activeMode    = activeDisplay.mode();
  activeMode.width   = width;
  activeMode.height  = height;
  return display::ActiveDisplay(activeDisplay.display(), activeMode);
}

TextBoxSpec createTextBoxSpecificationWithFontSize(std::uint16_t fontSize) {
  TextBoxSpec textBoxSpecification;
  textBoxSpecification.bounds   = {10, 20, 200, 80};
  textBoxSpecification.text     = "Framebuffer test";
  textBoxSpecification.fontSize = fontSize;
  return textBoxSpecification;
}

class LvglFramebufferRenderBackendTest : public ::testing::Test {
protected:
  LvglFramebufferRenderBackendTest()
      : framebufferDriver_(new NiceMock<MockLvglFramebufferDriver>),
        renderBackend_(makeLvglFramebufferRenderBackend(std::unique_ptr<ILvglFramebufferDriver>(framebufferDriver_))) {}

  void useFramebufferWithResolution(std::uint32_t width = 320U, std::uint32_t height = 240U,
                                    bool framebufferCanBeOpened = true) {
    EXPECT_CALL(*framebufferDriver_, createDisplay()).WillOnce(Invoke([this, width, height] {
      lv_display_t *lvglDisplay =
          lv_display_create(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
      const std::size_t drawBufferSize = static_cast<std::size_t>(width) * 20U * 4U;
      drawBuffer_.assign(drawBufferSize, 0U);
      lv_display_set_buffers(lvglDisplay, drawBuffer_.data(), nullptr, static_cast<std::uint32_t>(drawBuffer_.size()),
                             LV_DISPLAY_RENDER_MODE_PARTIAL);
      lv_display_set_flush_cb(lvglDisplay, [](lv_display_t *flushedDisplay, const lv_area_t *, std::uint8_t *) {
        lv_display_flush_ready(flushedDisplay);
      });
      return lvglDisplay;
    }));
    EXPECT_CALL(*framebufferDriver_, openFramebuffer(_, StrEq("/dev/fb0"))).WillOnce(Return(framebufferCanBeOpened));
  }

  NiceMock<MockLvglFramebufferDriver> *framebufferDriver_;
  std::unique_ptr<IRenderBackend>      renderBackend_;
  std::vector<std::uint8_t>            drawBuffer_;
};

TEST(LvglFramebufferRenderBackendConstructionTest, RequiresAFramebufferDriver) {
  EXPECT_THROW(makeLvglFramebufferRenderBackend(nullptr), std::invalid_argument);
}

TEST_F(LvglFramebufferRenderBackendTest, RejectsDrawingBeforeItIsInitialized) {
  EXPECT_THROW(renderBackend_->createTextBox(1U, createTextBoxSpecificationWithFontSize(24U)), std::logic_error);
  EXPECT_THROW(renderBackend_->updateTextBox(1U, "text"), std::logic_error);
  EXPECT_THROW(renderBackend_->moveTextBox(1U, 1, 2), std::logic_error);
  EXPECT_THROW(renderBackend_->deleteTextBox(1U), std::logic_error);
  EXPECT_THROW(renderBackend_->fillArea({{0, 0, 10, 10}, {1, 2, 3}}), std::logic_error);
  EXPECT_THROW(renderBackend_->showErrorScreen(createTextBoxSpecificationWithFontSize(24U)),
               std::logic_error);
  EXPECT_THROW(renderBackend_->clear({1, 2, 3}), std::logic_error);
  EXPECT_THROW(renderBackend_->processEventsAndGetWaitMilliseconds(), std::logic_error);
}

TEST_F(LvglFramebufferRenderBackendTest, CreatesUpdatesMovesAndDeletesTextBoxes) {
  useFramebufferWithResolution();
  renderBackend_->initialize(createActiveDisplayWithResolution(320U, 240U));

  const std::array<std::uint16_t, 4U> fontSizes = {14U, 20U, 24U, 32U};
  WidgetId                            textBoxId = 1U;
  for (const auto fontSize : fontSizes) {
    renderBackend_->createTextBox(textBoxId++, createTextBoxSpecificationWithFontSize(fontSize));
  }
  renderBackend_->updateTextBox(1U, "Updated text");
  renderBackend_->moveTextBox(1U, 25, 35);
  renderBackend_->deleteTextBox(1U);

  EXPECT_THROW(renderBackend_->updateTextBox(99U, "missing"), std::invalid_argument);
  EXPECT_THROW(renderBackend_->moveTextBox(99U, 1, 2), std::invalid_argument);
  EXPECT_THROW(renderBackend_->deleteTextBox(99U), std::invalid_argument);
  EXPECT_THROW(renderBackend_->initialize(createActiveDisplayWithResolution(320U, 240U)), std::logic_error);

  renderBackend_->shutdown();
  renderBackend_->shutdown();
}

TEST_F(LvglFramebufferRenderBackendTest, DrawsAreasErrorScreensAndClearsTheScreen) {
  useFramebufferWithResolution();
  renderBackend_->initialize(createActiveDisplayWithResolution(320U, 240U));

  renderBackend_->fillArea({{0, 0, 40, 50}, {10, 20, 30}});
  renderBackend_->createTextBox(1U, createTextBoxSpecificationWithFontSize(24U));
  renderBackend_->showErrorScreen(createTextBoxSpecificationWithFontSize(32U));
  EXPECT_THROW(renderBackend_->updateTextBox(1U, "The old application text box was removed"), std::invalid_argument);
  renderBackend_->showErrorScreen(createTextBoxSpecificationWithFontSize(24U));
  renderBackend_->clear({8, 13, 22});
  EXPECT_GE(renderBackend_->processEventsAndGetWaitMilliseconds(), 0U);

  TextBoxSpec invalidTextBoxSpecification  = createTextBoxSpecificationWithFontSize(24U);
  invalidTextBoxSpecification.bounds.width = 0;
  EXPECT_THROW(renderBackend_->createTextBox(1U, invalidTextBoxSpecification), std::invalid_argument);
  EXPECT_THROW(renderBackend_->fillArea({{0, 0, -1, 10}, {1, 2, 3}}), std::invalid_argument);
  EXPECT_THROW(renderBackend_->showErrorScreen(invalidTextBoxSpecification), std::invalid_argument);
}

TEST_F(LvglFramebufferRenderBackendTest, ReportsWhenLvglCannotCreateADisplay) {
  EXPECT_CALL(*framebufferDriver_, createDisplay()).WillOnce(Return(nullptr));

  EXPECT_THROW(renderBackend_->initialize(createActiveDisplayWithResolution(320U, 240U)), std::runtime_error);
}

TEST_F(LvglFramebufferRenderBackendTest, ReportsWhenTheFramebufferCannotBeOpened) {
  useFramebufferWithResolution(320U, 240U, false);

  EXPECT_THROW(renderBackend_->initialize(createActiveDisplayWithResolution(320U, 240U)), std::runtime_error);
}

TEST_F(LvglFramebufferRenderBackendTest, ReportsWhenFramebufferAndDisplayResolutionsDiffer) {
  useFramebufferWithResolution(320U, 240U);

  EXPECT_THROW(renderBackend_->initialize(createActiveDisplayWithResolution(640U, 480U)), std::runtime_error);
}

} // namespace
} // namespace ui
} // namespace iot
