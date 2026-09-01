#include "iot/display/display_manager.h"

#include "drm_object_ptr.h"
#include "edid_parser.h"
#include "internal/idrm_display_api.h"

#include <fcntl.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/*
 * Finds monitors and their modes through Linux DRM/KMS.
 *
 * This file only reads display information. Drawing is handled separately by
 * the LVGL framebuffer backend.
 */

namespace iot {
namespace display {
namespace {

/*
 * Small wrapper that closes a Linux file descriptor automatically.
 *
 * This also closes the descriptor when a function returns early or throws.
 */
class FileDescriptor {
public:
  /* Opens a DRM device such as /dev/dri/card0. */
  explicit FileDescriptor(const std::string &drmDevicePath)
      : m_fileDescriptor(::open(drmDevicePath.c_str(), O_RDWR | O_CLOEXEC)) {}

  /* Closes the device when it is open. */
  ~FileDescriptor() {
    if (m_fileDescriptor >= 0) {
      ::close(m_fileDescriptor);
    }
  }

  // Owns one file descriptor; copying and moving are disabled.
  FileDescriptor(const FileDescriptor &)            = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  FileDescriptor(FileDescriptor &&)                 = delete;
  FileDescriptor &operator=(FileDescriptor &&)      = delete;

  /* True when open() returned a usable descriptor. */
  bool valid() const noexcept {
    return m_fileDescriptor >= 0;
  }

  /* Raw descriptor passed to libdrm. */
  int get() const noexcept {
    return m_fileDescriptor;
  }

private:
  /* Linux file descriptor, or -1 when the device is not open. */
  int m_fileDescriptor{-1};
};

/* Copies a libdrm mode into the type used by the rest of the app. */
DisplayMode makeMode(const drmModeModeInfo &drmMode) {
  DisplayMode displayMode;
  displayMode.name          = drmMode.name;
  displayMode.width         = drmMode.hdisplay;
  displayMode.height        = drmMode.vdisplay;
  displayMode.refreshRateHz = drmMode.vrefresh;
  displayMode.preferred     = (drmMode.type & DRM_MODE_TYPE_PREFERRED) != 0U;
  displayMode.interlaced    = (drmMode.flags & DRM_MODE_FLAG_INTERLACE) != 0U;
  return displayMode;
}

/*
 * Finds the mode a connector is using right now.
 *
 * DRM stores the active mode on the CRTC, not directly on the connector. The
 * connector points to an encoder, and the encoder points to that CRTC. A
 * disconnected or unconfigured connector may not have either one.
 */
std::optional<DisplayMode> currentMode(int fd, const drmModeConnector &connector) {
  if (connector.encoder_id == 0U) {
    return std::nullopt;
  }
  internal::DrmEncoderPtr encoder{drmModeGetEncoder(fd, connector.encoder_id)};
  if (!encoder || encoder->crtc_id == 0U) {
    return std::nullopt;
  }
  internal::DrmCrtcPtr crtc{drmModeGetCrtc(fd, encoder->crtc_id)};
  if (!crtc || crtc->mode_valid == 0) {
    return std::nullopt;
  }
  return makeMode(crtc->mode);
}

/* Builds a Linux connector name such as HDMI-A-1. */
std::string makeConnectorName(const drmModeConnector &connector) {
  const char       *typeName = drmModeGetConnectorTypeName(connector.connector_type);
  const std::string prefix   = typeName == nullptr ? "Unknown" : typeName;
  return prefix + "-" + std::to_string(connector.connector_type_id);
}

/*
 * Finds the primary DRM devices under /dev/dri.
 *
 * Render-only nodes such as renderD128 are skipped because they cannot list
 * monitor connectors.
 */
std::vector<std::filesystem::path> findPrimaryDrmDevicePaths() {
  std::vector<std::filesystem::path> drmDevicePaths;
  std::error_code                    error;
  // Linux calls these devices card0, card1, and so on. "Card" also includes
  // built-in hardware such as the Raspberry Pi VC4 display device.
  const std::filesystem::path driDirectory{"/dev/dri"};
  if (!std::filesystem::exists(driDirectory, error)) {
    return drmDevicePaths;
  }

  for (const auto &drmDirectoryEntry : std::filesystem::directory_iterator(driDirectory, error)) {
    const std::string drmDeviceName = drmDirectoryEntry.path().filename().string();
    if (drmDeviceName.rfind("card", 0) != 0U || drmDeviceName.size() <= 4U) {
      continue;
    }

    bool cardNumberContainsOnlyDigits = true;
    for (std::size_t index = 4U; index < drmDeviceName.size(); ++index) {
      if (std::isdigit(static_cast<unsigned char>(drmDeviceName[index])) == 0) {
        cardNumberContainsOnlyDigits = false;
        break;
      }
    }
    if (cardNumberContainsOnlyDigits) {
      drmDevicePaths.push_back(drmDirectoryEntry.path());
    }
  }

  // Sort the paths so fallback display selection is predictable.
  std::sort(drmDevicePaths.begin(), drmDevicePaths.end());
  return drmDevicePaths;
}

/*
 * Finds every usable monitor connected to one DRM device.
 *
 * A monitor must be connected and have at least one mode. If one DRM device
 * cannot be opened, discovery continues with the other devices.
 */
std::vector<DisplayInfo> findConnectedDisplays(const std::filesystem::path &drmDevicePath) {
  FileDescriptor drmFileDescriptor{drmDevicePath.string()};
  if (!drmFileDescriptor.valid()) {
    return {};
  }
  internal::DrmResourcesPtr resources{drmModeGetResources(drmFileDescriptor.get())};
  if (!resources) {
    return {};
  }

  // DRM resources give us connector IDs. Read each connector separately to get
  // its state, modes, and monitor details.
  std::vector<DisplayInfo> connectedDisplays;
  for (int index = 0; index < resources->count_connectors; ++index) {
    internal::DrmConnectorPtr connector{drmModeGetConnector(drmFileDescriptor.get(), resources->connectors[index])};
    if (!connector || connector->connection != DRM_MODE_CONNECTED || connector->count_modes == 0) {
      continue;
    }

    // EDID is optional, so start with the information DRM always provides.
    DisplayInfo displayInformation;
    displayInformation.displayId.devicePath    = drmDevicePath.string();
    displayInformation.displayId.connectorName = makeConnectorName(*connector);
    displayInformation.displayId.connectorId   = connector->connector_id;
    displayInformation.physicalWidthMm         = connector->mmWidth;
    displayInformation.physicalHeightMm        = connector->mmHeight;
    displayInformation.currentMode             = currentMode(drmFileDescriptor.get(), *connector);

    const internal::EdidInfo edid   = internal::readEdid(drmFileDescriptor.get(), connector->connector_id);
    displayInformation.manufacturer = edid.manufacturer;
    displayInformation.model        = edid.model;
    displayInformation.serialNumber = edid.serial;

    // Keep the modes and preferred flag exactly as DRM reported them.
    displayInformation.supportedModes.reserve(static_cast<std::size_t>(connector->count_modes));
    for (int modeIndex = 0; modeIndex < connector->count_modes; ++modeIndex) {
      displayInformation.supportedModes.push_back(makeMode(connector->modes[modeIndex]));
    }
    connectedDisplays.push_back(std::move(displayInformation));
  }
  return connectedDisplays;
}

class LinuxDrmDisplayApi final : public internal::IDrmDisplayApi {
public:
  std::vector<DisplayInfo> connectedDisplays() const override {
    std::vector<DisplayInfo> allConnectedDisplays;
    for (const auto &drmDevicePath : findPrimaryDrmDevicePaths()) {
      auto displaysOnDrmDevice = findConnectedDisplays(drmDevicePath);
      for (auto &connectedDisplay : displaysOnDrmDevice) {
        allConnectedDisplays.push_back(std::move(connectedDisplay));
      }
    }
    return allConnectedDisplays;
  }
};

internal::IDrmDisplayApi &linuxDrmDisplayApi() {
  static LinuxDrmDisplayApi api;
  return api;
}

} // namespace

/* Saves the monitor details and current mode from one scan. */
ActiveDisplay::ActiveDisplay(DisplayInfo displayInformation, DisplayMode activeDisplayMode)
    : m_displayInformation(std::move(displayInformation)), m_activeDisplayMode(std::move(activeDisplayMode)) {}

/* Monitor details from the scan. */
const DisplayInfo &ActiveDisplay::display() const noexcept {
  return m_displayInformation;
}

/* Mode that was active during the scan. */
const DisplayMode &ActiveDisplay::mode() const noexcept {
  return m_activeDisplayMode;
}

DisplayManager::DisplayManager() : DisplayManager(linuxDrmDisplayApi()) {}

DisplayManager::DisplayManager(internal::IDrmDisplayApi &drmDisplayApi) : m_drmDisplayApi(drmDisplayApi) {}

std::vector<DisplayInfo> DisplayManager::connectedDisplays() const {
  return m_drmDisplayApi.connectedDisplays();
}

/*
 * Reads a monitor again and returns its current DRM mode.
 *
 * The cable may have changed since the first scan, so the monitor is checked
 * again. This app never changes modes and must use the mode DRM says is active,
 * not the preferred mode from EDID.
 */
ActiveDisplay DisplayManager::readActiveDisplay(const DisplayId &displayId) const {
  const auto connectedDisplayList = connectedDisplays();
  for (const auto &displayInformation : connectedDisplayList) {
    if (!(displayInformation.displayId == displayId)) {
      continue;
    }
    if (!displayInformation.currentMode) {
      throw std::runtime_error("Display " + displayId.connectorName +
                               " is connected but does not currently have an active DRM mode");
    }
    return ActiveDisplay{displayInformation, *displayInformation.currentMode};
  }
  throw std::runtime_error("Display " + displayId.connectorName + " is no longer connected");
}

} // namespace display
} // namespace iot
