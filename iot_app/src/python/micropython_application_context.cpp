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
    const std::vector<display::DisplayInfo>  &connectedDisplays,
    const system::ISystemInformationProvider &systemInformationProvider, system::SystemInformation systemInformation,
    std::string applicationName)
    : m_screenManager(&screenManager), m_activeDisplay(std::move(activeDisplay)),
      m_connectedDisplays(&connectedDisplays), m_systemInformationProvider(&systemInformationProvider),
      m_systemInformation(std::move(systemInformation)), m_applicationName(std::move(applicationName)) {
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
  return *m_screenManager;
}

std::uint32_t MicroPythonApplicationContext::displayWidth() const noexcept {
  return m_activeDisplay.mode().width;
}

std::uint32_t MicroPythonApplicationContext::displayHeight() const noexcept {
  return m_activeDisplay.mode().height;
}

const display::ActiveDisplay &MicroPythonApplicationContext::activeDisplay() const noexcept {
  return m_activeDisplay;
}

const std::vector<display::DisplayInfo> &MicroPythonApplicationContext::connectedDisplays() const noexcept {
  return *m_connectedDisplays;
}

const system::SystemInformation &MicroPythonApplicationContext::systemInformation() const noexcept {
  return m_systemInformation;
}

std::uint64_t MicroPythonApplicationContext::currentUptimeSeconds() const {
  return m_systemInformationProvider->readUptimeSeconds();
}

std::vector<system::NetworkInterfaceInformation> MicroPythonApplicationContext::readCurrentNetworkInterfaces() const {
  return m_systemInformationProvider->readNetworkInterfaces();
}

const std::string &MicroPythonApplicationContext::applicationName() const noexcept {
  return m_applicationName;
}

MicroPythonApplicationContext *MicroPythonApplicationContext::active() noexcept {
  return activeContext;
}

} // namespace python
} // namespace iot
