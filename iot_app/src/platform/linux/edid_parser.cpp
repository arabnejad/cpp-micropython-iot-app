#include "edid_parser.h"

#include "drm_object_ptr.h"

#include <xf86drmMode.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @file edid_parser.cpp
 *
 * Reads a monitor's EDID data and extracts the name, manufacturer, and serial
 * number used by IoT App.
 */

namespace iot {
namespace display {
namespace internal {
namespace {

/**
 * Turns a fixed-width EDID text field into a normal C++ string.
 *
 * EDID text is padded with newlines, null bytes, or spaces. Remove that padding
 * without changing the real text.
 */
std::string trimDescriptor(const std::uint8_t *bytes, std::size_t length) {
  std::string value(reinterpret_cast<const char *>(bytes), length);
  const auto  end = value.find_first_of("\n\r\0");
  if (end != std::string::npos) {
    value.resize(end);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

} // namespace

/**
 * Reads the manufacturer, model, and serial number from raw EDID bytes.
 *
 * Bad or incomplete EDID leaves the affected fields empty. A monitor with bad
 * EDID can still be used for drawing.
 */
EdidInfo parseEdidBytes(const void *data, std::size_t size) {
  EdidInfo result;

  // A base EDID block is always 128 bytes.
  if (data == nullptr || size < 128U) {
    return result;
  }

  const auto *bytes = static_cast<const std::uint8_t *>(data);
  // Every valid base block starts with this eight-byte header.
  constexpr std::array<std::uint8_t, 8> header{{0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}};
  if (!std::equal(header.begin(), header.end(), bytes)) {
    return result;
  }

  // The three manufacturer letters are packed into five bits each.
  const std::uint16_t manufacturerCode =
      static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[8]) << 8U) | static_cast<std::uint16_t>(bytes[9]));
  for (int shift : {10, 5, 0}) {
    const auto letter = static_cast<char>('A' + ((manufacturerCode >> shift) & 0x1fU) - 1U);
    if (letter >= 'A' && letter <= 'Z') {
      result.manufacturer.push_back(letter);
    }
  }

  // Four 18-byte descriptors start at byte 54. Tag 0xFC contains the monitor
  // name, and tag 0xFF contains its text serial number.
  for (std::size_t offset = 54; offset + 18U <= 126U; offset += 18U) {
    if (bytes[offset] != 0U || bytes[offset + 1U] != 0U || bytes[offset + 2U] != 0U) {
      continue;
    }
    const std::uint8_t tag = bytes[offset + 3U];
    if (tag == 0xfcU) {
      result.model = trimDescriptor(bytes + offset + 5U, 13U);
    } else if (tag == 0xffU) {
      result.serial = trimDescriptor(bytes + offset + 5U, 13U);
    }
  }

  // If there is no text serial number, try the older numeric field instead.
  if (result.serial.empty()) {
    const std::uint32_t serial = static_cast<std::uint32_t>(bytes[12]) | (static_cast<std::uint32_t>(bytes[13]) << 8U) |
                                 (static_cast<std::uint32_t>(bytes[14]) << 16U) |
                                 (static_cast<std::uint32_t>(bytes[15]) << 24U);
    if (serial != 0U) {
      result.serial = std::to_string(serial);
    }
  }
  return result;
}

EdidInfo readEdid(int fileDescriptor, std::uint32_t connectorId) {
  DrmObjectPropertiesPtr properties{drmModeObjectGetProperties(fileDescriptor, connectorId, DRM_MODE_OBJECT_CONNECTOR)};
  if (!properties) {
    return {};
  }

  for (std::uint32_t index = 0; index < properties->count_props; ++index) {
    DrmPropertyPtr property{drmModeGetProperty(fileDescriptor, properties->props[index])};
    if (!property || std::string(property->name) != "EDID" || properties->prop_values[index] == 0U) {
      continue;
    }

    DrmPropertyBlobPtr blob{
        drmModeGetPropertyBlob(fileDescriptor, static_cast<std::uint32_t>(properties->prop_values[index]))};
    if (blob) {
      return parseEdidBytes(blob->data, blob->length);
    }
  }
  return {};
}

} // namespace internal
} // namespace display
} // namespace iot
