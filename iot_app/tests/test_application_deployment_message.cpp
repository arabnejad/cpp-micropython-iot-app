#include "iot/messaging/application_deployment_message.h"

#include <gtest/gtest.h>

namespace iot {
namespace messaging {
namespace {

constexpr const char *validDeploymentMessageJson = R"json({
  "message_type":"install_single_file_application",
  "transfer_id":"transfer-42",
  "device_id":"raspberrypi-01",
  "application":{"id":"hello-world","name":"Hello world","entry_point":"main.py"},
  "source":{"encoding":"base64","size_bytes":15,"sha256":"03e693d9f2f687e0f40e36a8df7fcb4d1c22974012b7c2a55c000eb30f305824","content":"cHJpbnQoJ2hlbGxvJykK"}
})json";

TEST(ApplicationDeploymentMessageParserTest, ParsesACompleteMessageForTheExpectedDevice) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  const ApplicationDeploymentRequest deploymentRequest =
      deploymentMessageParser.parse(validDeploymentMessageJson, "raspberrypi-01");

  EXPECT_EQ(deploymentRequest.transferId, "transfer-42");
  EXPECT_EQ(deploymentRequest.applicationId, "hello-world");
  EXPECT_EQ(deploymentRequest.entryPoint, "main.py");
  EXPECT_EQ(deploymentRequest.sourceCode, "print('hello')\n");
}

TEST(ApplicationDeploymentMessageParserTest, RejectsAMessageForAnotherDevice) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  EXPECT_THROW(deploymentMessageParser.parse(validDeploymentMessageJson, "another-device"), std::runtime_error);
}

TEST(ApplicationDeploymentMessageParserTest, RejectsASourceWithTheWrongHash) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);
  std::string                              tamperedDeploymentMessageJson(validDeploymentMessageJson);
  tamperedDeploymentMessageJson.replace(tamperedDeploymentMessageJson.find("03e693"), 64U, 64U, '0');

  EXPECT_THROW(deploymentMessageParser.parse(tamperedDeploymentMessageJson, "raspberrypi-01"), std::runtime_error);
}

TEST(ApplicationDeploymentMessageParserTest, RecoversTransferIdFromAnOtherwiseInvalidMessage) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  const auto recoveredTransferId =
      deploymentMessageParser.tryReadTransferId(R"json({"transfer_id":"transfer-42"})json");

  ASSERT_TRUE(recoveredTransferId.has_value());
  EXPECT_EQ(*recoveredTransferId, "transfer-42");
}

TEST(ApplicationDeploymentMessageParserTest, SerializesStatusFieldsForTheSender) {
  const ApplicationDeploymentStatus deploymentStatus{"transfer-42", "started", "hello-world", "Application started"};

  const std::string serializedStatusPayload =
      ApplicationDeploymentMessageParser::serializeStatusPayload(deploymentStatus);

  EXPECT_NE(serializedStatusPayload.find("\"transfer_id\":\"transfer-42\""), std::string::npos);
  EXPECT_NE(serializedStatusPayload.find("\"status\":\"started\""), std::string::npos);
  EXPECT_NE(serializedStatusPayload.find("\"application_id\":\"hello-world\""), std::string::npos);
}

TEST(ApplicationDeploymentMessageParserTest, RejectsEmptyNonObjectDuplicateFieldAndUnsupportedTypeMessages) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  EXPECT_THROW(deploymentMessageParser.parse({}, "raspberrypi-01"), std::runtime_error);
  EXPECT_THROW(deploymentMessageParser.parse("[]", "raspberrypi-01"), std::runtime_error);
  EXPECT_THROW(deploymentMessageParser.parse(R"json({"transfer_id":"x","transfer_id":"x"})json", "raspberrypi-01"),
               std::runtime_error);

  std::string unsupportedType(validDeploymentMessageJson);
  unsupportedType.replace(unsupportedType.find("install_single_file_application"), 31U, "unsupported");
  EXPECT_THROW(deploymentMessageParser.parse(unsupportedType, "raspberrypi-01"), std::runtime_error);
}

TEST(ApplicationDeploymentMessageParserTest,
     RejectsUnsafeTransferIdsInvalidEncodingInvalidBase64WrongSourceSizeAndZeroLimit) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(14U);

  std::string unsafeTransferId(validDeploymentMessageJson);
  unsafeTransferId.replace(unsafeTransferId.find("transfer-42"), 11U, "../unsafe");
  EXPECT_THROW(deploymentMessageParser.parse(unsafeTransferId, "raspberrypi-01"), std::runtime_error);

  std::string wrongEncoding(validDeploymentMessageJson);
  wrongEncoding.replace(wrongEncoding.find("base64"), 6U, "text");
  EXPECT_THROW(deploymentMessageParser.parse(wrongEncoding, "raspberrypi-01"), std::runtime_error);

  std::string invalidBase64(validDeploymentMessageJson);
  invalidBase64.replace(invalidBase64.find("cHJpbnQoJ2hlbGxvJykK"), 20U, "bad!base64========");
  EXPECT_THROW(deploymentMessageParser.parse(invalidBase64, "raspberrypi-01"), std::runtime_error);

  std::string wrongDeclaredSize(validDeploymentMessageJson);
  wrongDeclaredSize.replace(wrongDeclaredSize.find("\"size_bytes\":15"), 15U, "\"size_bytes\":14");
  EXPECT_THROW(deploymentMessageParser.parse(wrongDeclaredSize, "raspberrypi-01"), std::runtime_error);

  EXPECT_FALSE(deploymentMessageParser.tryReadTransferId(R"json({"transfer_id":"../../unsafe"})json").has_value());
  EXPECT_FALSE(
      deploymentMessageParser.tryReadTransferId("{\"transfer_id\":\"" + std::string(129U, 'a') + "\"}").has_value());
  EXPECT_THROW(ApplicationDeploymentMessageParser(0U), std::invalid_argument);
}

TEST(ApplicationDeploymentMessageParserTest, DecodesBase64WithOneOrTwoPaddingBytes) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);
  const std::string                        messageWithOnePaddingByte =
      R"json({"message_type":"install_single_file_application","transfer_id":"one","device_id":"raspberrypi-01","application":{"id":"one","name":"One","entry_point":"main.py"},"source":{"encoding":"base64","size_bytes":2,"sha256":"8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4","content":"aGk="}})json";
  const std::string messageWithTwoPaddingBytes =
      R"json({"message_type":"install_single_file_application","transfer_id":"two","device_id":"raspberrypi-01","application":{"id":"two","name":"Two","entry_point":"main.py"},"source":{"encoding":"base64","size_bytes":1,"sha256":"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb","content":"YQ=="}})json";

  EXPECT_EQ(deploymentMessageParser.parse(messageWithOnePaddingByte, "raspberrypi-01").sourceCode, "hi");
  EXPECT_EQ(deploymentMessageParser.parse(messageWithTwoPaddingBytes, "raspberrypi-01").sourceCode, "a");
}

} // namespace
} // namespace messaging
} // namespace iot
