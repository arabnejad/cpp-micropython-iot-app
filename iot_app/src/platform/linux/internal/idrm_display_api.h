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
 * objects or requiring a connected monitor. Application code should use
 * IDisplayManager instead.
 */
class IDrmDisplayApi {
public:
  virtual ~IDrmDisplayApi()                                  = default;
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;
};

} // namespace internal
} // namespace display
} // namespace iot
