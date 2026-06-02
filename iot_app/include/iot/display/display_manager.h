#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {

class IDrmDisplayApi;

/** Provides monitor discovery without exposing libdrm to the rest of the app. */
class IDisplayManager {
public:
  virtual ~IDisplayManager() = default;

  /** Finds every connected monitor that has at least one usable mode. */
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;

  /** Reads the monitor again and returns the mode that is active right now. */
  virtual ActiveDisplay readActiveDisplay(const DisplayId &displayId) const = 0;
};

/** Linux implementation of display discovery using DRM/KMS. */
class DisplayManager final : public IDisplayManager {
public:
  DisplayManager();
  explicit DisplayManager(IDrmDisplayApi &drmDisplayApi);

  std::vector<DisplayInfo> connectedDisplays() const override;
  ActiveDisplay            readActiveDisplay(const DisplayId &displayId) const override;

private:
  IDrmDisplayApi &drmDisplayApi_;
};

} // namespace display
} // namespace iot
