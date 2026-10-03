#pragma once

#include "iot/input/seengreat_oled_hat.h"
#include "iot/system/system_information.h"

#include <memory>
#include <string>

namespace iot {
namespace status {

// A small independent 128x64 monochrome status screen, not an LVGL display.
class SeenGreatStatusDisplay {
public:
  explicit SeenGreatStatusDisplay(std::shared_ptr<input::SeenGreatOledHat> hat);
  // Allows verifying the exact OLED transfers without Raspberry Pi hardware.
  explicit SeenGreatStatusDisplay(hardware::II2cDevice &device);
  ~SeenGreatStatusDisplay() noexcept;
  void show(const system::ISystemInformationProvider &systemInfo, const std::string &applicationName);

private:
  std::shared_ptr<input::SeenGreatOledHat> m_hat;
  hardware::II2cDevice                    *m_device;
};

} // namespace status
} // namespace iot
