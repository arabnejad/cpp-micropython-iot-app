#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace iot {
namespace display {
namespace internal {

/* Monitor details read from EDID. */
struct EdidInfo {
  std::string manufacturer; ///< Three-letter manufacturer code, for example DEL.
  std::string model;        ///< Human-readable model name when available.
  std::string serial;       ///< Text or numeric serial number reported by the monitor.
};

/*
 * Reads monitor details from one raw EDID byte block.
 *
 * The data argument contains EDID information that has already been read from
 * a monitor or loaded from a file. This function only decodes those bytes; it
 * does not open a DRM device or communicate with the monitor. It can therefore
 * be used even when no monitor is currently connected.
 */
EdidInfo parseEdidBytes(const void *data, std::size_t size);

/*
 * Reads the EDID property for one DRM connector.
 *
 * Missing or invalid EDID returns empty fields. The monitor is still usable.
 */
EdidInfo readEdid(int fileDescriptor, std::uint32_t connectorId);

} // namespace internal
} // namespace display
} // namespace iot
