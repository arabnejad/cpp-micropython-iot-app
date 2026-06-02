#include "edid_parser.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace iot {
namespace display {
namespace internal {
namespace {

std::array<std::uint8_t, 128U> createEdidBlockWithValidHeader() {
  std::array<std::uint8_t, 128U>     edidBytes{};
  const std::array<std::uint8_t, 8U> edidHeader{{0x00U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x00U}};
  std::copy(edidHeader.begin(), edidHeader.end(), edidBytes.begin());
  return edidBytes;
}

TEST(EdidParserTest, ReadsManufacturerModelAndTextSerialFromAValidBaseBlock) {
  auto edidBytes = createEdidBlockWithValidHeader();
  edidBytes[8U]  = 0x05U; // AOC encoded as 1, 15, 3.
  edidBytes[9U]  = 0xe3U;
  edidBytes[57U] = 0xfcU;
  std::memcpy(edidBytes.data() + 59U, "Test monitor\n", 13U);
  edidBytes[75U] = 0xffU;
  std::memcpy(edidBytes.data() + 77U, "SERIAL-42\n", 10U);

  const EdidInfo parsedEdidInformation = parseEdidBytes(edidBytes.data(), edidBytes.size());

  EXPECT_EQ(parsedEdidInformation.manufacturer, "AOC");
  EXPECT_EQ(parsedEdidInformation.model, "Test monitor");
  EXPECT_EQ(parsedEdidInformation.serial, "SERIAL-42");
}

TEST(EdidParserTest, ReturnsEmptyInformationForAnInvalidHeader) {
  std::array<std::uint8_t, 128U> bytesWithoutValidHeader{};

  const EdidInfo parsedEdidInformation = parseEdidBytes(bytesWithoutValidHeader.data(), bytesWithoutValidHeader.size());

  EXPECT_TRUE(parsedEdidInformation.manufacturer.empty());
  EXPECT_TRUE(parsedEdidInformation.model.empty());
  EXPECT_TRUE(parsedEdidInformation.serial.empty());
}

TEST(EdidParserTest, UsesANumericSerialWhenNoTextSerialDescriptorExists) {
  auto edidBytes = createEdidBlockWithValidHeader();
  edidBytes[8U]  = 0x04U; // AEL encoded as 1, 5, 12.
  edidBytes[9U]  = 0xacU;
  edidBytes[12U] = 0x78U;
  edidBytes[13U] = 0x56U;
  edidBytes[14U] = 0x34U;
  edidBytes[15U] = 0x12U;
  edidBytes[57U] = 0xfcU;
  std::memcpy(edidBytes.data() + 59U, "Model  \n", 8U);

  const EdidInfo parsedEdidInformation = parseEdidBytes(edidBytes.data(), edidBytes.size());

  EXPECT_EQ(parsedEdidInformation.manufacturer, "AEL");
  EXPECT_EQ(parsedEdidInformation.model, "Model");
  EXPECT_EQ(parsedEdidInformation.serial, "305419896");
}

TEST(EdidParserTest, RejectsNullAndIncompleteDataAndIgnoresUnknownDescriptors) {
  EXPECT_TRUE(parseEdidBytes(nullptr, 128U).manufacturer.empty());
  std::array<std::uint8_t, 127U> incompleteEdidBytes{};
  EXPECT_TRUE(parseEdidBytes(incompleteEdidBytes.data(), incompleteEdidBytes.size()).manufacturer.empty());

  auto edidBytes                       = createEdidBlockWithValidHeader();
  edidBytes[57U]                       = 0xfeU;
  const EdidInfo parsedEdidInformation = parseEdidBytes(edidBytes.data(), edidBytes.size());
  EXPECT_TRUE(parsedEdidInformation.model.empty());
  EXPECT_TRUE(parsedEdidInformation.serial.empty());
}

TEST(EdidParserTest, ReturnsEmptyInformationWhenDrmCannotReadConnectorProperties) {
  const EdidInfo parsedEdidInformation = readEdid(-1, 1U);

  EXPECT_TRUE(parsedEdidInformation.manufacturer.empty());
  EXPECT_TRUE(parsedEdidInformation.model.empty());
  EXPECT_TRUE(parsedEdidInformation.serial.empty());
}

} // namespace
} // namespace internal
} // namespace display
} // namespace iot
