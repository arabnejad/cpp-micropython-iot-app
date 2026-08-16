#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iot {
namespace display {

/** Identifies one monitor connector on one Linux DRM device. */
struct DisplayId {
  /**
   * Path to a primary DRM device, normally `/dev/dri/cardN`.
   *
   * Linux calls every graphics device a "card". On a Raspberry Pi this is
   * normally the built-in display hardware, not a separate graphics card.
   */
  std::string   devicePath;
  std::string   connectorName;
  std::uint32_t connectorId{0};
};

inline bool operator==(const DisplayId &left, const DisplayId &right) {
  return left.devicePath == right.devicePath && left.connectorName == right.connectorName &&
         left.connectorId == right.connectorId;
}

/** Describes one resolution and refresh rate reported by DRM. */
struct DisplayMode {
  std::string   name;
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t refreshRateHz{0};
  bool          preferred{false};
  bool          interlaced{false};
};

/** Information reported for one connected monitor. */
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

/** Holds the selected monitor and the mode that is active on it now. */
class ActiveDisplay {
public:
  ActiveDisplay(DisplayInfo displayInformation, DisplayMode activeDisplayMode);

  const DisplayInfo &display() const noexcept;
  const DisplayMode &mode() const noexcept;

private:
  DisplayInfo displayInformation_;
  DisplayMode activeDisplayMode_;
};

} // namespace display
} // namespace iot
