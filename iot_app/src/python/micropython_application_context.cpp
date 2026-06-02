#include "iot/python/micropython_application_context.h"

#include "iot/ui/screen_manager.h"

#include <stdexcept>
#include <utility>

namespace iot {
namespace python {
namespace {

MicroPythonApplicationContext *activeContext = nullptr;

} // namespace

MicroPythonApplicationContext::MicroPythonApplicationContext(
    ui::ScreenManager &screenManager, display::ActiveDisplay activeDisplay,
    std::vector<display::DisplayInfo>         connectedDisplays,
    const system::ISystemInformationProvider &systemInformationProvider, system::SystemInformation systemInformation,
    std::string applicationName)
    : screenManager_(&screenManager), activeDisplay_(std::move(activeDisplay)),
      connectedDisplays_(std::move(connectedDisplays)), systemInformationProvider_(&systemInformationProvider),
      systemInformation_(std::move(systemInformation)), applicationName_(std::move(applicationName)) {
  if (activeContext != nullptr) {
    throw std::logic_error("Only one MicroPython application context can be active");
  }
  activeContext = this;
}

MicroPythonApplicationContext::~MicroPythonApplicationContext() {
  if (activeContext == this) {
    activeContext = nullptr;
  }
}

ui::ScreenManager &MicroPythonApplicationContext::screenManager() const noexcept {
  return *screenManager_;
}

std::uint32_t MicroPythonApplicationContext::displayWidth() const noexcept {
  return activeDisplay_.mode().width;
}

std::uint32_t MicroPythonApplicationContext::displayHeight() const noexcept {
  return activeDisplay_.mode().height;
}

const display::ActiveDisplay &MicroPythonApplicationContext::activeDisplay() const noexcept {
  return activeDisplay_;
}

const std::vector<display::DisplayInfo> &MicroPythonApplicationContext::connectedDisplays() const noexcept {
  return connectedDisplays_;
}

const system::SystemInformation &MicroPythonApplicationContext::systemInformation() const noexcept {
  return systemInformation_;
}

std::uint64_t MicroPythonApplicationContext::currentUptimeSeconds() const {
  return systemInformationProvider_->readUptimeSeconds();
}

std::vector<system::NetworkInterfaceInformation> MicroPythonApplicationContext::readCurrentNetworkInterfaces() const {
  return systemInformationProvider_->readNetworkInterfaces();
}

const std::string &MicroPythonApplicationContext::applicationName() const noexcept {
  return applicationName_;
}

MicroPythonApplicationContext *MicroPythonApplicationContext::active() noexcept {
  return activeContext;
}

} // namespace python
} // namespace iot
