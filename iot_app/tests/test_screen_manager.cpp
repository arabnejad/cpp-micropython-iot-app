#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

namespace iot {
namespace ui {
namespace {

bool waitForTextBox(tests::RecordingRenderBackend &recordingRenderBackend, WidgetId textBoxId) {
  return tests::waitUntil([&recordingRenderBackend, textBoxId] {
    std::lock_guard<std::mutex> lock(recordingRenderBackend.renderStateMutex);
    return recordingRenderBackend.textBoxesById.find(textBoxId) != recordingRenderBackend.textBoxesById.end();
  });
}

class FailingRenderBackend final : public IRenderBackend {
public:
  explicit FailingRenderBackend(bool shouldFailDuringInitialization)
      : shouldFailDuringInitialization_(shouldFailDuringInitialization) {}

  void initialize(const display::ActiveDisplay &) override {
    if (shouldFailDuringInitialization_) {
      throw std::runtime_error("renderer initialization failed");
    }
  }
  void shutdown() noexcept override {}
  void createTextBox(WidgetId, const TextBoxSpec &) override {
    throw std::runtime_error("renderer drawing failed");
  }
  void          updateTextBox(WidgetId, const std::string &) override {}
  void          moveTextBox(WidgetId, std::int32_t, std::int32_t) override {}
  void          deleteTextBox(WidgetId) override {}
  void          fillArea(const FilledAreaSpec &) override {}
  void          showErrorScreen(const TextBoxSpec &) override {}
  void          clear(Color) override {}
  std::uint32_t processEventsAndGetWaitMilliseconds() override {
    return 500U;
  }

private:
  bool shouldFailDuringInitialization_{false};
};

TEST(ScreenManagerTest, SendsTextBoxCreationAndUpdatesToTheRenderThread) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U);
  screenManager.start();

  TextBoxSpec textBoxSpecification;
  textBoxSpecification.text   = "First text";
  textBoxSpecification.bounds = {10, 20, 300, 80};
  const WidgetId textBoxId    = screenManager.drawTextBox(textBoxSpecification);
  ASSERT_TRUE(waitForTextBox(*recordingRenderBackendView, textBoxId));

  screenManager.updateTextBox(textBoxId, "Updated text");
  screenManager.moveTextBox(textBoxId, 40, 50);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    const auto                  textBox = recordingRenderBackendView->textBoxesById.find(textBoxId);
    return textBox != recordingRenderBackendView->textBoxesById.end() && textBox->second.text == "Updated text" &&
           textBox->second.bounds.x == 40 && textBox->second.bounds.y == 50;
  }));

  {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    EXPECT_EQ(recordingRenderBackendView->textBoxesById.at(textBoxId).text, "Updated text");
    EXPECT_EQ(recordingRenderBackendView->textBoxesById.at(textBoxId).bounds.x, 40);
    EXPECT_EQ(recordingRenderBackendView->textBoxesById.at(textBoxId).bounds.y, 50);
  }
  screenManager.stop();
  EXPECT_TRUE(recordingRenderBackendView->shutdownWasCalled);
}

TEST(ScreenManagerTest, RejectsDrawingBeforeTheRenderThreadStarts) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 1U);

  EXPECT_THROW(screenManager.drawTextBox({}), std::logic_error);
}

TEST(ScreenManagerTest, RequiresABackendAndANonZeroCommandLimit) {
  EXPECT_THROW(ScreenManager(tests::testActiveDisplay(), nullptr, 1U), std::invalid_argument);
  EXPECT_THROW(ScreenManager(tests::testActiveDisplay(), std::make_unique<tests::RecordingRenderBackend>(), 0U),
               std::invalid_argument);
}

TEST(ScreenManagerTest, SendsFillDeleteClearAndEmergencyScreenCommandsToTheRenderThread) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U);
  screenManager.start();

  const WidgetId textBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Temporary"});
  ASSERT_TRUE(waitForTextBox(*recordingRenderBackendView, textBoxId));
  screenManager.fillArea({{1, 2, 30, 40}, {3, 4, 5}});
  screenManager.deleteTextBox(textBoxId);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->drawnAreas.size() == 1U && recordingRenderBackendView->textBoxesById.empty();
  }));
  screenManager.clear({8, 13, 22});
  screenManager.showErrorScreen({{20, 20, 300, 200}, "Expected failure"});

  const bool allCommandsWereProcessed = tests::waitUntil([&] {
    std::lock_guard<std::mutex> lock(recordingRenderBackendView->renderStateMutex);
    return recordingRenderBackendView->drawnAreas.size() == 1U && recordingRenderBackendView->textBoxesById.empty() &&
           recordingRenderBackendView->lastErrorScreenText == "Expected failure";
  });
  screenManager.stop();
  EXPECT_TRUE(allCommandsWereProcessed);
}

TEST(ScreenManagerTest, LetsStartAndStopBeCalledMoreThanOnce) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 2U);
  screenManager.start();
  EXPECT_NO_THROW(screenManager.start());
  screenManager.stop();
  EXPECT_NO_THROW(screenManager.stop());
}

TEST(ScreenManagerTest, ReportsRenderBackendInitializationFailure) {
  ScreenManager screenManager(tests::testActiveDisplay(), std::make_unique<FailingRenderBackend>(true), 2U);
  EXPECT_THROW(screenManager.start(), std::runtime_error);
}

TEST(ScreenManagerTest, ReportsRenderBackendDrawingFailure) {
  ScreenManager screenManager(tests::testActiveDisplay(), std::make_unique<FailingRenderBackend>(false), 2U);
  screenManager.start();
  screenManager.drawTextBox({{0, 0, 10, 10}, "draw"});

  const bool drawingFailureWasReported = tests::waitUntil([&] {
    try {
      screenManager.throwIfRenderThreadFailed();
    } catch (const std::runtime_error &renderThreadFailure) {
      EXPECT_STREQ(renderThreadFailure.what(), "renderer drawing failed");
      return true;
    }
    return false;
  });

  screenManager.stop();
  EXPECT_TRUE(drawingFailureWasReported);
}

} // namespace
} // namespace ui
} // namespace iot
