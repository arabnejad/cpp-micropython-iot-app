#pragma once

#include "iot/hardware/i2c_device.h"
#include "iot/input/input_controller_base.h"

#include <memory>

namespace iot {
namespace input {

// Owns the HAT's D/C and reset GPIO lines and its I2C connection. A shared
// instance keeps these lines claimed while either input or display uses them.
class SeenGreatOledHat {
public:
  static std::shared_ptr<SeenGreatOledHat> open();
  ~SeenGreatOledHat();

  SeenGreatOledHat(const SeenGreatOledHat &)            = delete;
  SeenGreatOledHat &operator=(const SeenGreatOledHat &) = delete;

  hardware::II2cDevice &oled() noexcept;

private:
  SeenGreatOledHat();
  int                                  m_outputLines{-1};
  std::unique_ptr<hardware::I2cDevice> m_oled;
};

class SeenGreatOledHatController final : public InputControllerBase {
public:
  SeenGreatOledHatController();
  ~SeenGreatOledHatController() override;

  const char *modelName() const noexcept override;
  const char *boardType() const noexcept override;
  void        connect() override;
  void        refreshInputState() override;
  bool        isConnected() const noexcept override;

private:
  std::shared_ptr<SeenGreatOledHat> m_hat;
  int                               m_inputLines{-1};
};

} // namespace input
} // namespace iot
