#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace iot {
namespace display {
namespace internal {

/** Monitor name, manufacturer, and serial number read from EDID. */
struct EdidInfo {
  std::string manufacturer; ///< Three-letter manufacturer code, for example `DEL`.
  std::string model;        ///< Human-readable model name when available.
  std::string serial;       ///< Text or numeric serial number reported by the monitor.
};

/**
 * Reads monitor details from one raw EDID byte block.
 *
 * This helper has no DRM dependency. Keeping it separate lets the parser be
 * tested with known EDID bytes without needing a connected monitor.
 */
EdidInfo parseEdidBytes(const void *data, std::size_t size);

/**
 * Reads the EDID property for one DRM connector.
 *
 * Missing or invalid EDID is not fatal. The function returns empty fields and
 * the monitor can still be used.
 */
EdidInfo readEdid(int fileDescriptor, std::uint32_t connectorId);

} // namespace internal
} // namespace display
} // namespace iot
