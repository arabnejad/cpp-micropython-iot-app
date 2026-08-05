#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {
namespace internal {

/*
 * Internal wrapper around the libdrm calls used by DisplayManager.
 *
 * The normal implementation reads the real Linux DRM devices. Unit tests can
 * provide a short monitor list through this interface without creating libdrm
 * objects or requiring a connected monitor. Only DisplayManager and its tests
 * should use this interface. The rest of the application uses DisplayManager.
 */
class IDrmDisplayApi {
public:
  virtual ~IDrmDisplayApi()                                  = default;
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;
};

} // namespace internal
} // namespace display
} // namespace iot
