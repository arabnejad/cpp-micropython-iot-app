#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iot {
namespace display {

/* Identifies a monitor connector on a Linux DRM device. */
struct DisplayId {
  /*
   * Path to a primary DRM device, normally /dev/dri/cardN.
   *
   * Linux calls each graphics device a "card". On a Raspberry Pi this usually
   * means the built-in GPU called VideoCore IV (VC4) display hardware, not an add-in graphics card.
   */
  std::string   devicePath;
  std::string   connectorName;
  std::uint32_t connectorId{0};
};

inline bool operator==(const DisplayId &left, const DisplayId &right) {
  return left.devicePath == right.devicePath && left.connectorName == right.connectorName &&
         left.connectorId == right.connectorId;
}

/* Resolution and refresh rate reported by DRM. */
struct DisplayMode {
  std::string   name;
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t refreshRateHz{0};
  bool          preferred{false};
  bool          interlaced{false};
};

/* Details reported for a connected monitor. */
struct DisplayInfo {
  DisplayId                  displayId;
  std::string                manufacturer;
  std::string                model;
  std::string                serialNumber;
  std::uint32_t              physicalWidthMm{0};
  std::uint32_t              physicalHeightMm{0};
  std::optional<DisplayMode> currentMode;
  std::vector<DisplayMode>   supportedModes;
};

/* The selected monitor together with its current mode. */
class ActiveDisplay {
public:
  ActiveDisplay(DisplayInfo displayInformation, DisplayMode activeDisplayMode);

  const DisplayInfo &display() const noexcept;
  const DisplayMode &mode() const noexcept;

private:
  DisplayInfo m_displayInformation;
  DisplayMode m_activeDisplayMode;
};

} // namespace display
} // namespace iot
