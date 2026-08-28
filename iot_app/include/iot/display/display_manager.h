#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {

class IDrmDisplayApi;

/* Monitor discovery used by the rest of the application. */
class IDisplayManager {
public:
  virtual ~IDisplayManager() = default;

  /* Lists connected monitors that report at least one usable mode. */
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;

  /* Reads the monitor again to find its current mode. */
  virtual ActiveDisplay readActiveDisplay(const DisplayId &displayId) const = 0;
};

/* Finds monitors through Linux DRM/KMS. */
class DisplayManager final : public IDisplayManager {
public:
  DisplayManager();
  explicit DisplayManager(IDrmDisplayApi &drmDisplayApi);

  std::vector<DisplayInfo> connectedDisplays() const override;
  ActiveDisplay            readActiveDisplay(const DisplayId &displayId) const override;

private:
  IDrmDisplayApi &m_drmDisplayApi;
};

} // namespace display
} // namespace iot
