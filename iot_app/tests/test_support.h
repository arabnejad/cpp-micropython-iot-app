#pragma once

#include "iot/display/display_types.h"
#include "iot/system/system_information.h"
#include "iot/ui/render_backend.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace iot {
namespace tests {

/* Waits briefly for work performed by another thread to become visible. */
inline bool waitUntil(const std::function<bool()> &condition,
                      std::chrono::milliseconds    timeout = std::chrono::milliseconds(200)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return condition();
}

/* Removes a private temporary directory when a test ends. */
class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto uniqueName =
        "iot-app-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    m_path = std::filesystem::temp_directory_path() / uniqueName;
    std::filesystem::create_directories(m_path);
  }

  ~TemporaryDirectory() {
    std::error_code cleanupError;
    std::filesystem::remove_all(m_path, cleanupError);
  }

  TemporaryDirectory(const TemporaryDirectory &)            = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  const std::filesystem::path &path() const noexcept {
    return m_path;
  }

private:
  std::filesystem::path m_path;
};

/* Restores one environment variable when a test ends. */
class ScopedEnvironmentVariable {
public:
  ScopedEnvironmentVariable(const char *name, const char *value) : m_name(name) {
    const char *oldValue = std::getenv(name);
    if (oldValue != nullptr) {
      m_oldValue = oldValue;
      m_existed  = true;
    }
    if (value == nullptr) {
      ::unsetenv(name);
    } else {
      ::setenv(name, value, 1);
    }
  }

  ~ScopedEnvironmentVariable() {
    if (!m_existed) {
      ::unsetenv(m_name.c_str());
    } else {
      ::setenv(m_name.c_str(), m_oldValue.c_str(), 1);
    }
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable &)            = delete;
  ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

private:
  std::string m_name;
  std::string m_oldValue;
  bool        m_existed{false};
};

/* Small renderer used to check commands without opening /dev/fb0. */
class RecordingRenderBackend final : public ui::IRenderBackend {
public:
  void initialize(const display::ActiveDisplay &) override {
    wasInitialized = true;
  }
  void shutdown() noexcept override {
    shutdownWasCalled = true;
  }
  void createTextBox(ui::WidgetId textBoxId, const ui::TextBoxSpec &textBoxSpec) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    textBoxesById[textBoxId] = textBoxSpec;
  }
  void updateTextBox(ui::WidgetId textBoxId, const std::string &updatedText) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    textBoxesById.at(textBoxId).text = updatedText;
  }
  void moveTextBox(ui::WidgetId textBoxId, std::int32_t x, std::int32_t y) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    textBoxesById.at(textBoxId).bounds.x = x;
    textBoxesById.at(textBoxId).bounds.y = y;
  }
  void deleteTextBox(ui::WidgetId textBoxId) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    textBoxesById.erase(textBoxId);
  }
  void fillArea(const ui::FilledAreaSpec &filledAreaSpec) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    drawnAreas.push_back(filledAreaSpec);
  }
  void showErrorScreen(const ui::TextBoxSpec &errorBoxSpec) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    lastErrorScreenText = errorBoxSpec.text;
  }
  void clear(ui::Color) override {
    std::lock_guard<std::mutex> lock(renderStateMutex);
    textBoxesById.clear();
  }
  std::uint32_t processEventsAndGetWaitMilliseconds() override {
    return 1U;
  }

  bool                                    wasInitialized{false};
  bool                                    shutdownWasCalled{false};
  std::mutex                              renderStateMutex;
  std::map<ui::WidgetId, ui::TextBoxSpec> textBoxesById;
  std::vector<ui::FilledAreaSpec>         drawnAreas;
  std::string                             lastErrorScreenText;
};

inline display::ActiveDisplay testActiveDisplay() {
  display::DisplayInfo testDisplayInformation;
  testDisplayInformation.displayId        = {"/dev/dri/card0", "HDMI-A-1", 1U};
  testDisplayInformation.manufacturer     = "TST";
  testDisplayInformation.model            = "Test monitor";
  testDisplayInformation.serialNumber     = "MONITOR-1";
  testDisplayInformation.physicalWidthMm  = 600U;
  testDisplayInformation.physicalHeightMm = 340U;
  display::DisplayMode testDisplayMode;
  testDisplayMode.name               = "1920x1080";
  testDisplayMode.width              = 1920U;
  testDisplayMode.height             = 1080U;
  testDisplayMode.refreshRateHz      = 60U;
  testDisplayMode.preferred          = true;
  testDisplayInformation.currentMode = testDisplayMode;
  testDisplayInformation.supportedModes.push_back(testDisplayMode);
  return display::ActiveDisplay{testDisplayInformation, testDisplayMode};
}

inline std::vector<display::DisplayInfo> testConnectedDisplays() {
  return {testActiveDisplay().display()};
}

/* Fixed Linux values used by tests that call the native system module. */
class TestSystemInformationProvider final : public system::ISystemInformationProvider {
public:
  system::SystemInformation readSystemInformation() const override {
    system::SystemInformation testSystemInformation;
    testSystemInformation.hostname        = "test-device";
    testSystemInformation.uptimeSeconds   = 42U;
    testSystemInformation.logicalCpuCount = 4U;
    return testSystemInformation;
  }

  std::uint64_t readUptimeSeconds() const override {
    return 99U;
  }

  std::vector<system::NetworkInterfaceInformation> readNetworkInterfaces() const override {
    return {{"eth0", true, "192.0.2.10", 1000U}};
  }
};

} // namespace tests
} // namespace iot
