#pragma once

#include "iot/display/display_types.h"
#include "iot/system/system_information.h"

#include <string>
#include <vector>

namespace iot {
namespace ui {
class ScreenManager;
}

namespace python {

/**
 * Connects native MicroPython modules to the current C++ application.
 *
 * The MicroPython display and system modules call plain C functions in their
 * C++ bridge files. Those calls do not include objects such as ScreenManager
 * or the active display. This context keeps those objects in one place so the
 * bridge functions can find and use them.
 *
 * PythonApplicationRunner creates the context before it starts MicroPython.
 * Functions in display_cpp_bridge.cpp and system_cpp_bridge.cpp then call
 * active() whenever Python asks for display or system work. The runner removes
 * the context after the MicroPython interpreter has stopped.
 *
 * Only one context can be active because IoT App runs one Python application
 * at a time.
 */
class MicroPythonApplicationContext {
public:
  MicroPythonApplicationContext(ui::ScreenManager &screenManager, display::ActiveDisplay activeDisplay,
                                std::vector<display::DisplayInfo>         connectedDisplays,
                                const system::ISystemInformationProvider &systemInformationProvider,
                                system::SystemInformation systemInformation, std::string applicationName);
  ~MicroPythonApplicationContext();

  // Only one context can be active; copying and moving are disabled.
  MicroPythonApplicationContext(const MicroPythonApplicationContext &)            = delete;
  MicroPythonApplicationContext &operator=(const MicroPythonApplicationContext &) = delete;
  MicroPythonApplicationContext(MicroPythonApplicationContext &&)                 = delete;
  MicroPythonApplicationContext &operator=(MicroPythonApplicationContext &&)      = delete;

  ui::ScreenManager &screenManager() const noexcept;
  std::uint32_t displayWidth() const noexcept;
  std::uint32_t displayHeight() const noexcept;
  const display::ActiveDisplay &activeDisplay() const noexcept;
  const std::vector<display::DisplayInfo> &connectedDisplays() const noexcept;
  const system::SystemInformation &systemInformation() const noexcept;
  /** Reads the current Linux uptime without rebuilding the full snapshot. */
  std::uint64_t currentUptimeSeconds() const;
  /** Reads the current Linux network-interface state. */
  std::vector<system::NetworkInterfaceInformation> readCurrentNetworkInterfaces() const;
  const std::string &applicationName() const noexcept;
  /** Gets the active context, or `nullptr` while no Python app is running. */
  static MicroPythonApplicationContext *active() noexcept;

private:
  ui::ScreenManager                        *screenManager_{nullptr};
  display::ActiveDisplay                    activeDisplay_;
  std::vector<display::DisplayInfo>         connectedDisplays_;
  const system::ISystemInformationProvider *systemInformationProvider_{nullptr};
  system::SystemInformation                 systemInformation_;
  std::string                               applicationName_;
};

} // namespace python
} // namespace iot
