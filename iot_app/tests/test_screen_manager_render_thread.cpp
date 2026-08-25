#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

namespace iot {
namespace ui {
namespace {

class FailingRenderBackend final : public IRenderBackend {
public:
  explicit FailingRenderBackend(bool shouldFailDuringInitialization)
      : m_shouldFailDuringInitialization(shouldFailDuringInitialization) {}

  void initialize(const display::ActiveDisplay &) override {
    if (m_shouldFailDuringInitialization) {
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
  void          createJpegImage(WidgetId, const DecodedJpegImageSpec &) override {}
  void          replaceJpegImage(WidgetId, std::shared_ptr<const DecodedJpegImage>) override {}
  void          moveJpegImage(WidgetId, std::int32_t, std::int32_t) override {}
  void          deleteJpegImage(WidgetId) override {}
  void          setBackgroundJpegImage(const DecodedBackgroundJpegImageSpec &) override {}
  void          clearBackgroundJpegImage() override {}
  void          fillArea(const FilledAreaSpec &) override {}
  void          showErrorScreen(const TextBoxSpec &) override {}
  void          clear(Color) override {}
  std::uint32_t processEventsAndGetWaitMilliseconds() override {
    return 500U;
  }

private:
  bool m_shouldFailDuringInitialization{false};
};

/* Pauses the first event-processing call so the test can fill the queue. */
class RenderBatchObservationBackend final : public tests::RecordingRenderBackend {
public:
  void fillArea(const FilledAreaSpec &filledAreaSpec) override {
    tests::RecordingRenderBackend::fillArea(filledAreaSpec);

    std::lock_guard<std::mutex> observationLock(m_observationMutex);
    ++m_numberOfDrawingCommandsProcessed;
    if (m_numberOfDrawingCommandsProcessed == 17U) {
      m_backendProcessedEventsBeforeSeventeenthCommand = m_numberOfEventProcessingCalls >= 2U;
      m_observationChanged.notify_all();
    }
  }

  std::uint32_t processEventsAndGetWaitMilliseconds() override {
    std::unique_lock<std::mutex> observationLock(m_observationMutex);
    ++m_numberOfEventProcessingCalls;
    m_observationChanged.notify_all();

    if (m_numberOfEventProcessingCalls == 1U) {
      m_observationChanged.wait(observationLock, [this] { return m_firstEventProcessingCallMayReturn; });
    }
    return 1U;
  }

  bool waitUntilFirstEventProcessingCallStarts() {
    std::unique_lock<std::mutex> observationLock(m_observationMutex);
    return m_observationChanged.wait_for(observationLock, std::chrono::seconds(2),
                                         [this] { return m_numberOfEventProcessingCalls >= 1U; });
  }

  void letFirstEventProcessingCallReturn() {
    {
      std::lock_guard<std::mutex> observationLock(m_observationMutex);
      m_firstEventProcessingCallMayReturn = true;
    }
    m_observationChanged.notify_all();
  }

  bool waitUntilSeventeenthCommandIsProcessed() {
    std::unique_lock<std::mutex> observationLock(m_observationMutex);
    return m_observationChanged.wait_for(observationLock, std::chrono::seconds(2),
                                         [this] { return m_numberOfDrawingCommandsProcessed >= 17U; });
  }

  bool backendProcessedEventsBeforeSeventeenthCommand() const {
    std::lock_guard<std::mutex> observationLock(m_observationMutex);
    return m_backendProcessedEventsBeforeSeventeenthCommand;
  }

private:
  mutable std::mutex      m_observationMutex;
  std::condition_variable m_observationChanged;
  std::size_t             m_numberOfEventProcessingCalls{0U};
  std::size_t             m_numberOfDrawingCommandsProcessed{0U};
  bool                    m_firstEventProcessingCallMayReturn{false};
  bool                    m_backendProcessedEventsBeforeSeventeenthCommand{false};
};

TEST(ScreenManagerRenderThreadTest, ProcessesBackendEventsBetweenRenderCommandBatches) {
  auto          renderBackend     = std::make_unique<RenderBatchObservationBackend>();
  auto         *renderBackendView = renderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(renderBackend), 32U);
  screenManager.start();
  const bool firstEventProcessingCallStarted = renderBackendView->waitUntilFirstEventProcessingCallStarts();

  // The render loop handles 16 commands per batch. Command 17 should run only
  // after the backend has had another chance to process LVGL events.
  for (std::size_t commandNumber = 0U; commandNumber < 17U; ++commandNumber) {
    screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}});
  }

  // Release the backend before an assertion can end the test, so stop() can
  // always join the render thread.
  renderBackendView->letFirstEventProcessingCallReturn();
  ASSERT_TRUE(firstEventProcessingCallStarted);
  ASSERT_TRUE(renderBackendView->waitUntilSeventeenthCommandIsProcessed());
  EXPECT_TRUE(renderBackendView->backendProcessedEventsBeforeSeventeenthCommand());
  screenManager.stop();
}

TEST(ScreenManagerRenderThreadTest, RejectsDrawingBeforeTheRenderThreadStarts) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 1U);

  EXPECT_THROW(screenManager.drawTextBox({{0, 0, 100, 40}, "Not started"}), std::logic_error);
}

TEST(ScreenManagerRenderThreadTest, RequiresABackendAndANonZeroCommandLimit) {
  EXPECT_THROW(ScreenManager(tests::testActiveDisplay(), nullptr, 1U), std::invalid_argument);
  EXPECT_THROW(ScreenManager(tests::testActiveDisplay(), std::make_unique<tests::RecordingRenderBackend>(), 0U),
               std::invalid_argument);
}

TEST(ScreenManagerRenderThreadTest, LetsStartAndStopBeCalledMoreThanOnce) {
  auto          recordingRenderBackend = std::make_unique<tests::RecordingRenderBackend>();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 2U);
  screenManager.start();
  EXPECT_NO_THROW(screenManager.start());
  screenManager.stop();
  EXPECT_NO_THROW(screenManager.stop());
}

TEST(ScreenManagerRenderThreadTest, ReportsRenderBackendInitializationFailure) {
  ScreenManager screenManager(tests::testActiveDisplay(), std::make_unique<FailingRenderBackend>(true), 2U);
  EXPECT_THROW(screenManager.start(), std::runtime_error);
}

TEST(ScreenManagerRenderThreadTest, ReportsRenderBackendDrawingFailure) {
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
