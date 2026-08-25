#pragma once

#include "iot/python/python_application_manager.h"
#include "iot/ui/screen_manager.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace iot {
namespace python {

inline PythonApplication createPythonApplication(std::string applicationName, std::string sourceCode) {
  PythonApplication pythonApplication;
  pythonApplication.applicationId   = "test-application";
  pythonApplication.applicationName = std::move(applicationName);
  pythonApplication.entryPointPath  = "main.py";
  pythonApplication.sourceCode      = std::move(sourceCode);
  return pythonApplication;
}

class PythonApplicationManagerTest : public ::testing::Test {
protected:
  PythonApplicationManagerTest()
      : m_recordingRenderBackend(std::make_unique<tests::RecordingRenderBackend>()),
        m_recordingRenderBackendView(m_recordingRenderBackend.get()),
        m_screenManager(tests::testActiveDisplay(), std::move(m_recordingRenderBackend), 32U) {}

  void SetUp() override {
    m_screenManager.start();
  }

  void TearDown() override {
    m_screenManager.stop();
  }

  PythonApplicationManager
  createApplicationManager(std::vector<display::DisplayInfo> connectedDisplays = tests::testConnectedDisplays()) {
    return PythonApplicationManager(m_screenManager, tests::testActiveDisplay(), std::move(connectedDisplays),
                                    m_systemInformationProvider, m_fileDownloader, 256U * 1024U);
  }

  bool waitForEmergencyScreen() {
    return tests::waitUntil([this] {
      std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
      return !m_recordingRenderBackendView->lastErrorScreenText.empty();
    });
  }

  std::string emergencyScreenText() {
    std::lock_guard<std::mutex> renderStateLock(m_recordingRenderBackendView->renderStateMutex);
    return m_recordingRenderBackendView->lastErrorScreenText;
  }

  std::unique_ptr<tests::RecordingRenderBackend> m_recordingRenderBackend;
  tests::RecordingRenderBackend                 *m_recordingRenderBackendView;
  ui::ScreenManager                              m_screenManager;
  tests::TestSystemInformationProvider           m_systemInformationProvider;
  tests::TestFileDownloader                      m_fileDownloader;
};

} // namespace python
} // namespace iot
