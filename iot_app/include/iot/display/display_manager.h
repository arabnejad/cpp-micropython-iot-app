#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {

namespace internal {
class IDrmDisplayApi;
}

/* Finds monitors through Linux DRM/KMS. */
class DisplayManager final {
public:
  DisplayManager();
  explicit DisplayManager(internal::IDrmDisplayApi &drmDisplayApi);

  /* Scans DRM and returns connected monitors with their current and supported modes. */
  std::vector<DisplayInfo> connectedDisplays() const;

private:
  internal::IDrmDisplayApi &m_drmDisplayApi;
};

} // namespace display
} // namespace iot
