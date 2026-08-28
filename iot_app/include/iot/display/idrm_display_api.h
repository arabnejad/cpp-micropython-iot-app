#pragma once

#include "iot/display/display_types.h"

#include <vector>

namespace iot {
namespace display {

/*
 * Small wrapper around the libdrm calls needed by DisplayManager.
 * Other parts of the application should use IDisplayManager. This lower level
 * interface exists so DisplayManager does not depend directly on
 * libdrm calls.
 */
class IDrmDisplayApi {
public:
  virtual ~IDrmDisplayApi()                                  = default;
  virtual std::vector<DisplayInfo> connectedDisplays() const = 0;
};

} // namespace display
} // namespace iot
