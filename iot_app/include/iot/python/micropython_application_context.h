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

/*
 * Gives native MicroPython modules access to the current C++ services.
 *
 * The display and system modules enter C++ through plain C bridge functions.
 * Those functions cannot receive ScreenManager and the other service objects
 * as normal C++ arguments, so they read them from the active context.
 *
 * Before MicroPython starts, PythonApplicationManager makes this context
 * active. When Python requests display or system work, the bridge calls
 * active() to get the required C++ services. The context stays active until
 * MicroPython has completely stopped.
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

  ui::ScreenManager                       &screenManager() const noexcept;
  std::uint32_t                            displayWidth() const noexcept;
  std::uint32_t                            displayHeight() const noexcept;
  const display::ActiveDisplay            &activeDisplay() const noexcept;
  const std::vector<display::DisplayInfo> &connectedDisplays() const noexcept;
  const system::SystemInformation         &systemInformation() const noexcept;
  /* Reads the current Linux uptime without rebuilding the full snapshot. */
  std::uint64_t currentUptimeSeconds() const;
  /* Reads the current Linux network-interface state. */
  std::vector<system::NetworkInterfaceInformation> readCurrentNetworkInterfaces() const;
  const std::string                               &applicationName() const noexcept;
  /* Gets the active context, or nullptr when no Python app is running. */
  static MicroPythonApplicationContext *active() noexcept;

private:
  ui::ScreenManager                        *m_screenManager{nullptr};
  display::ActiveDisplay                    m_activeDisplay;
  std::vector<display::DisplayInfo>         m_connectedDisplays;
  const system::ISystemInformationProvider *m_systemInformationProvider{nullptr};
  system::SystemInformation                 m_systemInformation;
  std::string                               m_applicationName;
};

} // namespace python
} // namespace iot
