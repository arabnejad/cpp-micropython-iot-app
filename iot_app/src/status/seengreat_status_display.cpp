#include "iot/status/seengreat_status_display.h"
#include "fonts.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace iot {
namespace status {
namespace {

// Compact 5x7 font for the status rows; the inverse title uses the supplied
// 11x16 bitmap font from the SeenGreat sample.
using Glyph = std::array<std::uint8_t, 5>;
Glyph glyph(char ch) {
  switch (ch) {
  case '0':
    return {{0x3e, 0x51, 0x49, 0x45, 0x3e}};
  case '1':
    return {{0x00, 0x42, 0x7f, 0x40, 0x00}};
  case '2':
    return {{0x42, 0x61, 0x51, 0x49, 0x46}};
  case '3':
    return {{0x21, 0x41, 0x45, 0x4b, 0x31}};
  case '4':
    return {{0x18, 0x14, 0x12, 0x7f, 0x10}};
  case '5':
    return {{0x27, 0x45, 0x45, 0x45, 0x39}};
  case '6':
    return {{0x3c, 0x4a, 0x49, 0x49, 0x30}};
  case '7':
    return {{0x01, 0x71, 0x09, 0x05, 0x03}};
  case '8':
    return {{0x36, 0x49, 0x49, 0x49, 0x36}};
  case '9':
    return {{0x06, 0x49, 0x49, 0x29, 0x1e}};
  case 'A':
    return {{0x7e, 0x11, 0x11, 0x11, 0x7e}};
  case 'B':
    return {{0x7f, 0x49, 0x49, 0x49, 0x36}};
  case 'C':
    return {{0x3e, 0x41, 0x41, 0x41, 0x22}};
  case 'D':
    return {{0x7f, 0x41, 0x41, 0x22, 0x1c}};
  case 'E':
    return {{0x7f, 0x49, 0x49, 0x49, 0x41}};
  case 'F':
    return {{0x7f, 0x09, 0x09, 0x09, 0x01}};
  case 'G':
    return {{0x3e, 0x41, 0x49, 0x49, 0x7a}};
  case 'H':
    return {{0x7f, 0x08, 0x08, 0x08, 0x7f}};
  case 'I':
    return {{0x00, 0x41, 0x7f, 0x41, 0x00}};
  case 'J':
    return {{0x20, 0x40, 0x41, 0x3f, 0x01}};
  case 'K':
    return {{0x7f, 0x08, 0x14, 0x22, 0x41}};
  case 'L':
    return {{0x7f, 0x40, 0x40, 0x40, 0x40}};
  case 'M':
    return {{0x7f, 0x02, 0x0c, 0x02, 0x7f}};
  case 'N':
    return {{0x7f, 0x04, 0x08, 0x10, 0x7f}};
  case 'O':
    return {{0x3e, 0x41, 0x41, 0x41, 0x3e}};
  case 'P':
    return {{0x7f, 0x09, 0x09, 0x09, 0x06}};
  case 'Q':
    return {{0x3e, 0x41, 0x51, 0x21, 0x5e}};
  case 'R':
    return {{0x7f, 0x09, 0x19, 0x29, 0x46}};
  case 'S':
    return {{0x46, 0x49, 0x49, 0x49, 0x31}};
  case 'T':
    return {{0x01, 0x01, 0x7f, 0x01, 0x01}};
  case 'U':
    return {{0x3f, 0x40, 0x40, 0x40, 0x3f}};
  case 'V':
    return {{0x1f, 0x20, 0x40, 0x20, 0x1f}};
  case 'W':
    return {{0x7f, 0x20, 0x18, 0x20, 0x7f}};
  case 'X':
    return {{0x63, 0x14, 0x08, 0x14, 0x63}};
  case 'Y':
    return {{0x07, 0x08, 0x70, 0x08, 0x07}};
  case 'Z':
    return {{0x61, 0x51, 0x49, 0x45, 0x43}};
  case 'a':
    return {{0x20, 0x54, 0x54, 0x54, 0x78}};
  case 'b':
    return {{0x7f, 0x48, 0x44, 0x44, 0x38}};
  case 'c':
    return {{0x38, 0x44, 0x44, 0x44, 0x20}};
  case 'd':
    return {{0x38, 0x44, 0x44, 0x48, 0x7f}};
  case 'e':
    return {{0x38, 0x54, 0x54, 0x54, 0x18}};
  case 'f':
    return {{0x08, 0x7e, 0x09, 0x01, 0x02}};
  case 'g':
    return {{0x0c, 0x52, 0x52, 0x52, 0x3e}};
  case 'h':
    return {{0x7f, 0x08, 0x04, 0x04, 0x78}};
  case 'i':
    return {{0x00, 0x44, 0x7d, 0x40, 0x00}};
  case 'j':
    return {{0x20, 0x40, 0x44, 0x3d, 0x00}};
  case 'k':
    return {{0x7f, 0x10, 0x28, 0x44, 0x00}};
  case 'l':
    return {{0x00, 0x41, 0x7f, 0x40, 0x00}};
  case 'm':
    return {{0x7c, 0x04, 0x18, 0x04, 0x78}};
  case 'n':
    return {{0x7c, 0x08, 0x04, 0x04, 0x78}};
  case 'o':
    return {{0x38, 0x44, 0x44, 0x44, 0x38}};
  case 'p':
    return {{0x7c, 0x14, 0x14, 0x14, 0x08}};
  case 'q':
    return {{0x08, 0x14, 0x14, 0x18, 0x7c}};
  case 'r':
    return {{0x7c, 0x08, 0x04, 0x04, 0x08}};
  case 's':
    return {{0x48, 0x54, 0x54, 0x54, 0x20}};
  case 't':
    return {{0x04, 0x3f, 0x44, 0x40, 0x20}};
  case 'u':
    return {{0x3c, 0x40, 0x40, 0x20, 0x7c}};
  case 'v':
    return {{0x1c, 0x20, 0x40, 0x20, 0x1c}};
  case 'w':
    return {{0x3c, 0x40, 0x30, 0x40, 0x3c}};
  case 'x':
    return {{0x44, 0x28, 0x10, 0x28, 0x44}};
  case 'y':
    return {{0x0c, 0x50, 0x50, 0x50, 0x3c}};
  case 'z':
    return {{0x44, 0x64, 0x54, 0x4c, 0x44}};
  case ':':
    return {{0x00, 0x36, 0x36, 0x00, 0x00}};
  case '.':
    return {{0x00, 0x60, 0x60, 0x00, 0x00}};
  case '?':
    return {{0x02, 0x01, 0x51, 0x09, 0x06}};
  case '-':
    return {{0x08, 0x08, 0x08, 0x08, 0x08}};
  case '_':
    return {{0x40, 0x40, 0x40, 0x40, 0x40}};
  default:
    return {{0, 0, 0, 0, 0}};
  }
}

using Frame = std::array<std::uint8_t, 128 * 8>;
void pixel(Frame &frame, unsigned x, unsigned y, bool lit) {
  if (x >= 128 || y >= 64)
    return;
  auto      &byte = frame[(y / 8) * 128 + x];
  const auto bit  = static_cast<std::uint8_t>(1U << (y % 8));
  if (lit)
    byte |= bit;
  else
    byte &= static_cast<std::uint8_t>(~bit);
}

void horizontalLine(Frame &frame, unsigned y) {
  for (unsigned x = 1; x < 127; ++x)
    pixel(frame, x, y, true);
}

void draw(Frame &frame, unsigned x, unsigned y, const std::string &text) {
  for (char ch : text) {
    if (x + 5 > 128 || y + 7 > 64)
      break;
    const Glyph pixels = glyph(ch);
    for (unsigned column = 0; column < 5; ++column)
      for (unsigned row = 0; row < 7; ++row)
        if (pixels[column] & (1U << row))
          pixel(frame, x + column, y + row, true);
    x += 6;
  }
}

// Font16 is row-major, with two bytes per 11-pixel row and the first pixel in
// bit 7. Dark glyphs are drawn onto the already lit title band.
void drawInverseTitle(Frame &frame) {
  constexpr char     title[]     = "IoT App";
  constexpr unsigned titleLength = sizeof(title) - 1;
  const unsigned     startX      = (128 - titleLength * Font16.Width) / 2;
  for (unsigned y = 16; y < 40; ++y)
    for (unsigned x = 0; x < 128; ++x)
      pixel(frame, x, y, true);

  for (unsigned character = 0; character < titleLength; ++character) {
    const auto     ascii       = static_cast<unsigned char>(title[character]);
    const unsigned glyphOffset = (static_cast<unsigned>(ascii) - 32U) * Font16.Height * 2U;
    for (unsigned row = 0; row < Font16.Height; ++row)
      for (unsigned column = 0; column < Font16.Width; ++column)
        if (Font16.table[glyphOffset + row * 2 + column / 8] & (0x80U >> (column % 8))) {
          const unsigned x = startX + character * Font16.Width + column;
          pixel(frame, x, 20 + row, false);
          pixel(frame, x + 1, 20 + row, false); // Thicken only the inverse title by one pixel.
        }
  }
}

void drawConnectionIcon(Frame &frame, bool connected) {
  constexpr const char *arcs[] = {"001111111100", "110000000011", "000000000000", "000111111000",
                                  "001000001100", "000000000000", "000001100000", "000001100000"};
  for (unsigned y = 0; y < 8; ++y)
    for (unsigned x = 0; x < 12; ++x)
      if (arcs[y][x] == '1')
        pixel(frame, 114 + x, 3 + y, true);
  if (!connected)
    for (unsigned offset = 0; offset < 8; ++offset)
      pixel(frame, 115 + offset, 3 + offset, false);
}

std::string clippedAppName(const std::string &name) {
  // 4 characters for "APP ", then 17 characters for the name (21*6 <= 128).
  constexpr std::size_t maxNameCharacters = 17;
  std::string           printable;
  for (char raw : name) {
    if (printable.size() == maxNameCharacters + 1)
      break;
    const auto ch = static_cast<unsigned char>(raw);
    printable.push_back(ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?');
  }
  if (printable.size() > maxNameCharacters)
    return printable.substr(0, maxNameCharacters - 3) + "...";
  return printable;
}

std::string cpuTemperatureText(const std::optional<double> temperatureCelsius) {
  if (!temperatureCelsius || !std::isfinite(*temperatureCelsius) || *temperatureCelsius < -40.0 ||
      *temperatureCelsius > 150.0)
    return "CPU --";
  char text[16]{};
  std::snprintf(text, sizeof(text), "CPU %.0fC", *temperatureCelsius);
  return text;
}

void command(hardware::II2cDevice &device, std::initializer_list<std::uint8_t> commands) {
  std::vector<std::uint8_t> bytes{0x00};
  bytes.insert(bytes.end(), commands.begin(), commands.end());
  device.write(bytes);
}

void initialize(hardware::II2cDevice &device) {
  // SH1106, 128x64; page addressing and two hidden columns to the left.
  command(device, {0xae, 0x02, 0x10, 0x40, 0x81, 0xa0, 0xc0, 0xa6, 0xa8, 0x3f, 0xd3, 0x00, 0xd5,
                   0x80, 0xd9, 0xf1, 0xda, 0x12, 0xdb, 0x40, 0x20, 0x02, 0xa4, 0xa6, 0xaf});
}

} // namespace

SeenGreatStatusDisplay::SeenGreatStatusDisplay(std::shared_ptr<input::SeenGreatOledHat> hat)
    : m_hat(std::move(hat)), m_device(m_hat ? &m_hat->oled() : nullptr) {
  if (!m_hat)
    throw std::invalid_argument("OLED display requires its HAT");
  initialize(*m_device);
}

SeenGreatStatusDisplay::SeenGreatStatusDisplay(hardware::II2cDevice &device) : m_device(&device) {
  initialize(*m_device);
}

SeenGreatStatusDisplay::~SeenGreatStatusDisplay() noexcept {
  // 0xAE puts the SH1106 into display-off / power-save mode. The owned HAT
  // still holds GPIO25 high and keeps I2C open until this destructor finishes.
  try {
    command(*m_device, {0xae});
  } catch (...) {
    // An I2C failure during shutdown must not interrupt the rest of cleanup.
  }
}

void SeenGreatStatusDisplay::show(const system::ISystemInformationProvider &info, const std::string &applicationName) {
  Frame       frame{};
  std::string ip        = "--";
  bool        connected = false;
  try {
    for (const auto &network : info.readNetworkInterfaces()) {
      if (network.connected && !network.ipv4Address.empty()) {
        ip        = network.ipv4Address;
        connected = true;
        break;
      }
    }
  } catch (const std::exception &) {
    // Network information may be temporarily unavailable. Retry next time.
  }
  draw(frame, 2, 3, "IP " + ip);
  drawConnectionIcon(frame, connected);
  drawInverseTitle(frame);
  draw(frame, 3, 43, "APP " + clippedAppName(applicationName));
  horizontalLine(frame, 53);
  std::string temperature = "CPU --";
  try {
    temperature = cpuTemperatureText(info.readSystemInformation().cpuTemperatureCelsius);
  } catch (const std::exception &) {
    // Show the temperature again after the next successful read.
  }
  draw(frame, 2, 57, temperature);
  for (unsigned y = 57; y < 64; ++y)
    pixel(frame, 64, y, true);
  std::string localTime = "--:--:--";
  try {
    localTime = info.readCurrentLocalTime();
  } catch (const std::exception &) {
    // Keep the placeholder until the clock can be read again.
  }
  draw(frame, 78, 57, localTime.size() >= 8 ? localTime.substr(localTime.size() - 8) : "--:--:--");

  auto &device = *m_device;
  for (unsigned page = 0; page < 8; ++page) {
    command(device, {static_cast<std::uint8_t>(0xb0 + page), 0x02, 0x10});
    std::vector<std::uint8_t> bytes{0x40};
    bytes.insert(bytes.end(), frame.begin() + page * 128, frame.begin() + (page + 1) * 128);
    device.write(bytes);
  }
}

} // namespace status
} // namespace iot
