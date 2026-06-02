#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {

/** Reads connected displays from Linux DRM/KMS. */
class IDrmDisplayApi {
public:
  virtual ~IDrmDisplayApi()                                  = default;
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;
};

/** Returns the Linux DRM implementation used outside tests. */
IDrmDisplayApi &drmDisplayApi();

} // namespace display
} // namespace iot
