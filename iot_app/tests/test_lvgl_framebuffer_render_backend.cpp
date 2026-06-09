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
      : m_framebufferDriver(new NiceMock<MockLvglFramebufferDriver>),
        m_renderBackend(
            makeLvglFramebufferRenderBackend(std::unique_ptr<ILvglFramebufferDriver>(m_framebufferDriver))) {}

  void useFramebufferWithResolution(std::uint32_t width = 320U, std::uint32_t height = 240U,
                                    bool framebufferCanBeOpened = true) {
    EXPECT_CALL(*m_framebufferDriver, createDisplay()).WillOnce(Invoke([this, width, height] {
      lv_display_t *lvglDisplay =
          lv_display_create(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
      const std::size_t drawBufferSize = static_cast<std::size_t>(width) * 20U * 4U;
      m_drawBuffer.assign(drawBufferSize, 0U);
      lv_display_set_buffers(lvglDisplay, m_drawBuffer.data(), nullptr, static_cast<std::uint32_t>(m_drawBuffer.size()),
                             LV_DISPLAY_RENDER_MODE_PARTIAL);
      lv_display_set_flush_cb(lvglDisplay, [](lv_display_t *flushedDisplay, const lv_area_t *, std::uint8_t *) {
        lv_display_flush_ready(flushedDisplay);
      });
      return lvglDisplay;
    }));
    EXPECT_CALL(*m_framebufferDriver, openFramebuffer(_, StrEq("/dev/fb0"))).WillOnce(Return(framebufferCanBeOpened));
  }

  NiceMock<MockLvglFramebufferDriver> *m_framebufferDriver;
  std::unique_ptr<IRenderBackend>      m_renderBackend;
  std::vector<std::uint8_t>            m_drawBuffer;
};

TEST(LvglFramebufferRenderBackendConstructionTest, RequiresAFramebufferDriver) {
  EXPECT_THROW(makeLvglFramebufferRenderBackend(nullptr), std::invalid_argument);
}

TEST_F(LvglFramebufferRenderBackendTest, RejectsDrawingBeforeItIsInitialized) {
  EXPECT_THROW(m_renderBackend->createTextBox(1U, createTextBoxSpecificationWithFontSize(24U)), std::logic_error);
  EXPECT_THROW(m_renderBackend->updateTextBox(1U, "text"), std::logic_error);
  EXPECT_THROW(m_renderBackend->moveTextBox(1U, 1, 2), std::logic_error);
  EXPECT_THROW(m_renderBackend->deleteTextBox(1U), std::logic_error);
  EXPECT_THROW(m_renderBackend->fillArea({{0, 0, 10, 10}, {1, 2, 3}}), std::logic_error);
  EXPECT_THROW(m_renderBackend->showErrorScreen(createTextBoxSpecificationWithFontSize(24U)), std::logic_error);
  EXPECT_THROW(m_renderBackend->clear({1, 2, 3}), std::logic_error);
  EXPECT_THROW(m_renderBackend->processEventsAndGetWaitMilliseconds(), std::logic_error);
}

TEST_F(LvglFramebufferRenderBackendTest, CreatesUpdatesMovesAndDeletesTextBoxes) {
  useFramebufferWithResolution();
  m_renderBackend->initialize(createActiveDisplayWithResolution(320U, 240U));

  const std::array<std::uint16_t, 4U> fontSizes = {14U, 20U, 24U, 32U};
  WidgetId                            textBoxId = 1U;
  for (const auto fontSize : fontSizes) {
    m_renderBackend->createTextBox(textBoxId++, createTextBoxSpecificationWithFontSize(fontSize));
  }
  m_renderBackend->updateTextBox(1U, "Updated text");
  m_renderBackend->moveTextBox(1U, 25, 35);
  m_renderBackend->deleteTextBox(1U);

  EXPECT_THROW(m_renderBackend->updateTextBox(99U, "missing"), std::invalid_argument);
  EXPECT_THROW(m_renderBackend->moveTextBox(99U, 1, 2), std::invalid_argument);
  EXPECT_THROW(m_renderBackend->deleteTextBox(99U), std::invalid_argument);
  EXPECT_THROW(m_renderBackend->initialize(createActiveDisplayWithResolution(320U, 240U)), std::logic_error);

  m_renderBackend->shutdown();
  m_renderBackend->shutdown();
}

TEST_F(LvglFramebufferRenderBackendTest, DrawsAreasErrorScreensAndClearsTheScreen) {
  useFramebufferWithResolution();
  m_renderBackend->initialize(createActiveDisplayWithResolution(320U, 240U));

  m_renderBackend->fillArea({{0, 0, 40, 50}, {10, 20, 30}});
  m_renderBackend->createTextBox(1U, createTextBoxSpecificationWithFontSize(24U));
  m_renderBackend->showErrorScreen(createTextBoxSpecificationWithFontSize(32U));
  EXPECT_THROW(m_renderBackend->updateTextBox(1U, "The old application text box was removed"), std::invalid_argument);
  m_renderBackend->showErrorScreen(createTextBoxSpecificationWithFontSize(24U));
  m_renderBackend->clear({8, 13, 22});
  EXPECT_GE(m_renderBackend->processEventsAndGetWaitMilliseconds(), 0U);

  TextBoxSpec invalidTextBoxSpecification  = createTextBoxSpecificationWithFontSize(24U);
  invalidTextBoxSpecification.bounds.width = 0;
  EXPECT_THROW(m_renderBackend->createTextBox(1U, invalidTextBoxSpecification), std::invalid_argument);
  EXPECT_THROW(m_renderBackend->fillArea({{0, 0, -1, 10}, {1, 2, 3}}), std::invalid_argument);
  EXPECT_THROW(m_renderBackend->showErrorScreen(invalidTextBoxSpecification), std::invalid_argument);
}

TEST_F(LvglFramebufferRenderBackendTest, ReportsWhenLvglCannotCreateADisplay) {
  EXPECT_CALL(*m_framebufferDriver, createDisplay()).WillOnce(Return(nullptr));

  EXPECT_THROW(m_renderBackend->initialize(createActiveDisplayWithResolution(320U, 240U)), std::runtime_error);
}

TEST_F(LvglFramebufferRenderBackendTest, ReportsWhenTheFramebufferCannotBeOpened) {
  useFramebufferWithResolution(320U, 240U, false);

  EXPECT_THROW(m_renderBackend->initialize(createActiveDisplayWithResolution(320U, 240U)), std::runtime_error);
}

TEST_F(LvglFramebufferRenderBackendTest, ReportsWhenFramebufferAndDisplayResolutionsDiffer) {
  useFramebufferWithResolution(320U, 240U);

  EXPECT_THROW(m_renderBackend->initialize(createActiveDisplayWithResolution(640U, 480U)), std::runtime_error);
}

} // namespace
} // namespace ui
} // namespace iot
