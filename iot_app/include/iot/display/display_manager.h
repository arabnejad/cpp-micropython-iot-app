#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {

namespace internal {
class IDrmDisplayApi;
}

/* Monitor discovery used by the rest of the application. */
class IDisplayManager {
public:
  virtual ~IDisplayManager() = default;

  /* Scans DRM and returns connected monitors with their current and supported modes. */
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;
};

/* Finds monitors through Linux DRM/KMS. */
class DisplayManager final : public IDisplayManager {
public:
  DisplayManager();
  explicit DisplayManager(internal::IDrmDisplayApi &drmDisplayApi);

  std::vector<DisplayInfo> connectedDisplays() const override;

private:
  internal::IDrmDisplayApi &m_drmDisplayApi;
};

} // namespace display
} // namespace iot
