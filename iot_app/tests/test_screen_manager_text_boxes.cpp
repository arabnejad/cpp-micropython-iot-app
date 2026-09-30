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

bool waitForDrawnAreas(tests::RecordingRenderBackend &recordingRenderBackend, std::size_t count) {
  return tests::waitUntil([&recordingRenderBackend, count] {
    std::lock_guard<std::mutex> lock(recordingRenderBackend.renderStateMutex);
    return recordingRenderBackend.drawnAreas.size() == count;
  });
}

TEST(ScreenManagerFilledAreaTest, LimitsRenderedAreasAndResetsOnClearEmergencyScreenAndRestart) {
  auto          backend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *backendView = backend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(backend), 256U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();

  for (std::size_t round = 0U; round < 3U; ++round) {
    for (std::size_t area = 0U; area < 128U; ++area) {
      ASSERT_NO_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}));
    }
    ASSERT_TRUE(waitForDrawnAreas(*backendView, (round + 1U) * 128U));
    // An empty queue must not allow more rectangles on the same screen.
    EXPECT_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}), std::runtime_error);
    EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());

    if (round == 0U) {
      screenManager.clear({0, 0, 0});
    } else if (round == 1U) {
      screenManager.showErrorScreen({{0, 0, 100, 40}, "Test error"});
    } else {
      screenManager.stop();
      screenManager.start();
    }
  }

  EXPECT_NO_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}));
  EXPECT_TRUE(waitForDrawnAreas(*backendView, 385U));
}

TEST(ScreenManagerFilledAreaTest, CountsQueuedAreasAndReleasesThemWhenClearDiscardsTheQueue) {
  auto          backend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *backendView = backend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(backend), 256U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();
  const bool rendererPaused = backendView->waitUntilRenderThreadIsPaused();

  for (std::size_t round = 0U; round < 2U; ++round) {
    for (std::size_t area = 0U; area < 128U; ++area) {
      EXPECT_NO_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}));
    }
    EXPECT_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}), std::runtime_error);
    if (round == 0U) {
      EXPECT_NO_THROW(screenManager.clear({0, 0, 0}));
    }
  }

  // Resume before assertions that could return, so stop() can join the thread.
  backendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  EXPECT_TRUE(waitForDrawnAreas(*backendView, 128U));
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
}

TEST(ScreenManagerFilledAreaTest, RequestsRejectedByAFullQueueDoNotUseTheAreaLimit) {
  auto          backend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *backendView = backend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(backend), 1U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();
  const bool rendererPaused = backendView->waitUntilRenderThreadIsPaused();

  EXPECT_NO_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}));
  for (std::size_t attempt = 0U; attempt < 128U; ++attempt) {
    EXPECT_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}), std::runtime_error);
  }

  backendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  ASSERT_TRUE(waitForDrawnAreas(*backendView, 1U));
  for (std::size_t count = 2U; count <= 128U; ++count) {
    ASSERT_NO_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}));
    ASSERT_TRUE(waitForDrawnAreas(*backendView, count));
  }
  EXPECT_THROW(screenManager.fillArea({{0, 0, 10, 10}, {1, 2, 3}}), std::runtime_error);
}

TEST(ScreenManagerTextBoxTest, SendsTextBoxCreationAndUpdatesToTheRenderThread) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
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

TEST(ScreenManagerTextBoxTest, RejectsInvalidTextBoxIdsAndSizesBeforeTheyReachTheRenderer) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();

  // Bad requests must not remove an existing text box. Negative positions
  // remain valid: only the part inside the screen is visible.
  const auto textBoxId = screenManager.drawTextBox({{-10, -20, 100, 40}, "Still drawing"});

  for (const WidgetId invalidTextBoxId : {0U, 999U}) {
    EXPECT_THROW(screenManager.updateTextBox(invalidTextBoxId, "Invalid"), std::invalid_argument);
    EXPECT_THROW(screenManager.moveTextBox(invalidTextBoxId, 10, 20), std::invalid_argument);
    EXPECT_THROW(screenManager.deleteTextBox(invalidTextBoxId), std::invalid_argument);
  }
  for (const Rect invalidBounds : {Rect{0, 0, 0, 40}, Rect{0, 0, -1, 40}, Rect{0, 0, 100, 0}, Rect{0, 0, 100, -1}}) {
    EXPECT_THROW(screenManager.drawTextBox({invalidBounds, "Invalid size"}), std::invalid_argument);
    EXPECT_THROW(screenManager.fillArea({invalidBounds, {0, 0, 0}}), std::invalid_argument);
    EXPECT_THROW(screenManager.showErrorScreen({invalidBounds, "Invalid error box"}), std::invalid_argument);
  }

  screenManager.moveTextBox(textBoxId, -30, -40);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(recordingRenderBackendView->renderStateMutex);
    const auto                  textBox = recordingRenderBackendView->textBoxesById.find(textBoxId);
    return textBox != recordingRenderBackendView->textBoxesById.end() && textBox->second.bounds.x == -30;
  }));
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
}

TEST(ScreenManagerTextBoxTest, UpdatesMovesAndDeletesATextBoxBeforeItsCreationIsRendered) {
  auto          pausedRenderBackend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *pausedRenderBackendView = pausedRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(pausedRenderBackend), 8U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();
  const bool rendererPaused = pausedRenderBackendView->waitUntilRenderThreadIsPaused();

  WidgetId deletedTextBoxId = 0U;
  EXPECT_NO_THROW(deletedTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Before rendering"}));
  EXPECT_NO_THROW(screenManager.updateTextBox(deletedTextBoxId, "Changed before rendering"));
  EXPECT_NO_THROW(screenManager.moveTextBox(deletedTextBoxId, 20, 30));
  EXPECT_NO_THROW(screenManager.deleteTextBox(deletedTextBoxId));
  EXPECT_THROW(screenManager.updateTextBox(deletedTextBoxId, "Deleted"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveTextBox(deletedTextBoxId, 40, 50), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteTextBox(deletedTextBoxId), std::invalid_argument);
  WidgetId replacementTextBoxId = 0U;
  EXPECT_NO_THROW(replacementTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Replacement"}));

  // Resume before assertions that could return from the test, so stop() can join.
  pausedRenderBackendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  ASSERT_TRUE(waitForTextBox(*pausedRenderBackendView, replacementTextBoxId));
  {
    std::lock_guard<std::mutex> renderStateLock(pausedRenderBackendView->renderStateMutex);
    EXPECT_EQ(pausedRenderBackendView->textBoxesById.count(deletedTextBoxId), 0U);
  }
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
}

TEST(ScreenManagerTextBoxTest, ClearAndEmergencyScreenInvalidateTextBoxIdsWhoseCreationWasStillQueued) {
  auto          pausedRenderBackend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *pausedRenderBackendView = pausedRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(pausedRenderBackend), 8U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();
  const bool rendererPaused = pausedRenderBackendView->waitUntilRenderThreadIsPaused();

  WidgetId clearedTextBoxId = 0U;
  EXPECT_NO_THROW(clearedTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Discarded by clear"}));
  EXPECT_NO_THROW(screenManager.clear({0, 0, 0}));
  EXPECT_THROW(screenManager.updateTextBox(clearedTextBoxId, "Stale"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveTextBox(clearedTextBoxId, 10, 20), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteTextBox(clearedTextBoxId), std::invalid_argument);

  WidgetId emergencyReplacedTextBoxId = 0U;
  EXPECT_NO_THROW(emergencyReplacedTextBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Discarded by emergency"}));
  EXPECT_NO_THROW(screenManager.showErrorScreen({{0, 0, 300, 200}, "Application failed"}));
  EXPECT_THROW(screenManager.updateTextBox(emergencyReplacedTextBoxId, "Stale"), std::invalid_argument);
  EXPECT_THROW(screenManager.moveTextBox(emergencyReplacedTextBoxId, 10, 20), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteTextBox(emergencyReplacedTextBoxId), std::invalid_argument);

  pausedRenderBackendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(pausedRenderBackendView->renderStateMutex);
    return pausedRenderBackendView->textBoxesById.empty() &&
           pausedRenderBackendView->lastErrorScreenText == "Application failed";
  }));
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
}

TEST(ScreenManagerTextBoxTest, RejectedCreationLeavesNoTextBoxIdAndRejectedDeletionKeepsTheExistingId) {
  auto          pausedRenderBackend     = std::make_unique<tests::PausedRecordingRenderBackend>();
  auto         *pausedRenderBackendView = pausedRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(pausedRenderBackend), 1U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
  screenManager.start();
  const bool rendererPaused = pausedRenderBackendView->waitUntilRenderThreadIsPaused();

  WidgetId textBoxId = 0U;
  EXPECT_NO_THROW(textBoxId = screenManager.drawTextBox({{0, 0, 100, 40}, "Fills the queue"}));
  EXPECT_THROW(screenManager.drawTextBox({{0, 0, 100, 40}, "No queue space"}), std::runtime_error);
  // Even a guessed ID must not refer to a creation that the queue rejected.
  EXPECT_THROW(screenManager.updateTextBox(textBoxId + 1U, "Was never queued"), std::invalid_argument);
  EXPECT_THROW(screenManager.deleteTextBox(textBoxId), std::runtime_error);

  pausedRenderBackendView->letRenderThreadContinue();
  ASSERT_TRUE(rendererPaused);
  ASSERT_TRUE(waitForTextBox(*pausedRenderBackendView, textBoxId));
  EXPECT_NO_THROW(screenManager.updateTextBox(textBoxId, "Deletion was rejected, so this still exists"));
  ASSERT_TRUE(tests::waitUntil([&] {
    std::lock_guard<std::mutex> renderStateLock(pausedRenderBackendView->renderStateMutex);
    return pausedRenderBackendView->textBoxesById.at(textBoxId).text == "Deletion was rejected, so this still exists";
  }));
  EXPECT_NO_THROW(screenManager.deleteTextBox(textBoxId));
  EXPECT_THROW(screenManager.deleteTextBox(textBoxId), std::invalid_argument);
  EXPECT_NO_THROW(screenManager.throwIfRenderThreadFailed());
}

TEST(ScreenManagerTextBoxTest, SendsFillDeleteClearAndEmergencyScreenCommandsToTheRenderThread) {
  auto          recordingRenderBackend     = std::make_unique<tests::RecordingRenderBackend>();
  auto         *recordingRenderBackendView = recordingRenderBackend.get();
  ScreenManager screenManager(tests::testActiveDisplay(), std::move(recordingRenderBackend), 16U,
                              std::make_unique<tests::RecordingExclusiveVideoPlayer>());
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

} // namespace
} // namespace ui
} // namespace iot
