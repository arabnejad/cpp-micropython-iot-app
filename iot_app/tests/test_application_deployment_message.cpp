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

void expectParsingError(const ApplicationDeploymentMessageParser &parser, const std::string &message,
                        const char *expectedError) {
  try {
    parser.parse(message, "raspberrypi-01");
    FAIL() << "Expected parsing to fail: " << expectedError;
  } catch (const std::runtime_error &error) {
    EXPECT_STREQ(error.what(), expectedError);
  }
}

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

TEST(ApplicationDeploymentMessageParserTest, RejectsTransferIdsThatCannotNameAnApplicationDirectory) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  for (const std::string &transferId :
       {std::string{}, std::string{"."}, std::string{".."}, std::string{".staging-transfer-42"},
        std::string{"nested/transfer"}, std::string(129U, 'a')}) {
    SCOPED_TRACE(transferId);
    std::string message(validDeploymentMessageJson);
    message.replace(message.find("transfer-42"), 11U, transferId);

    EXPECT_THROW(deploymentMessageParser.parse(message, "raspberrypi-01"), std::runtime_error);
    EXPECT_FALSE(deploymentMessageParser.tryReadTransferId(message).has_value());
  }
}

TEST(ApplicationDeploymentMessageParserTest, AcceptsTransferIdsWithDotsAndAtTheLengthLimit) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  for (const std::string &transferId : {std::string{"release.v2"}, std::string(128U, 'a')}) {
    SCOPED_TRACE(transferId);
    std::string message(validDeploymentMessageJson);
    message.replace(message.find("transfer-42"), 11U, transferId);

    EXPECT_EQ(deploymentMessageParser.parse(message, "raspberrypi-01").transferId, transferId);
  }
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
  const ApplicationDeploymentStatus deploymentStatus{"transfer-42", "accepted", "hello-world",
                                                     "Application received and ready to execute"};

  const std::string serializedStatusPayload =
      ApplicationDeploymentMessageParser::serializeStatusPayload(deploymentStatus);

  EXPECT_NE(serializedStatusPayload.find("\"transfer_id\":\"transfer-42\""), std::string::npos);
  EXPECT_NE(serializedStatusPayload.find("\"status\":\"accepted\""), std::string::npos);
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

TEST(ApplicationDeploymentMessageParserTest, RejectsTextAfterTheJsonObject) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);

  EXPECT_THROW(deploymentMessageParser.parse(std::string(validDeploymentMessageJson) + " extra", "raspberrypi-01"),
               std::runtime_error);
}

TEST(ApplicationDeploymentMessageParserTest, RejectsUnsafeTransferIdsWhenParsingAndRecoveringMessages) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);
  std::string                              unsafeTransferId(validDeploymentMessageJson);
  unsafeTransferId.replace(unsafeTransferId.find("transfer-42"), 11U, "../unsafe");
  expectParsingError(deploymentMessageParser, unsafeTransferId, "Deployment transfer_id is not a safe directory name");

  EXPECT_FALSE(deploymentMessageParser.tryReadTransferId(R"json({"transfer_id":"../../unsafe"})json").has_value());
  EXPECT_FALSE(
      deploymentMessageParser.tryReadTransferId("{\"transfer_id\":\"" + std::string(129U, 'a') + "\"}").has_value());
}

TEST(ApplicationDeploymentMessageParserTest, RejectsUnsupportedSourceEncoding) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);
  std::string                              wrongEncoding(validDeploymentMessageJson);
  wrongEncoding.replace(wrongEncoding.find("base64"), 6U, "text");
  expectParsingError(deploymentMessageParser, wrongEncoding, "Deployment source encoding is not supported");
}

TEST(ApplicationDeploymentMessageParserTest, RejectsInvalidBase64LengthAndCharacters) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);
  std::string                              invalidBase64(validDeploymentMessageJson);
  invalidBase64.replace(invalidBase64.find("cHJpbnQoJ2hlbGxvJykK"), 20U, "bad!base64========");
  expectParsingError(deploymentMessageParser, invalidBase64, "Deployment source is not valid Base64 text");

  // Keep the original length so the parser reaches the character check.
  std::string invalidCharacter(validDeploymentMessageJson);
  invalidCharacter[invalidCharacter.find("cHJpbnQoJ2hlbGxvJykK")] = '!';
  expectParsingError(deploymentMessageParser, invalidCharacter,
                     "Deployment source contains an invalid Base64 character");
}

TEST(ApplicationDeploymentMessageParserTest, RejectsDeclaredSizeThatDoesNotMatchDecodedSource) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(1024U);
  std::string                              wrongDeclaredSize(validDeploymentMessageJson);
  wrongDeclaredSize.replace(wrongDeclaredSize.find("\"size_bytes\":15"), 15U, "\"size_bytes\":14");
  expectParsingError(deploymentMessageParser, wrongDeclaredSize,
                     "Decoded Python source size does not match size_bytes");
}

TEST(ApplicationDeploymentMessageParserTest, AcceptsSourceAtTheLimitAndRejectsLargerDeclaredSize) {
  const ApplicationDeploymentMessageParser parserAtLimit(15U);
  EXPECT_EQ(parserAtLimit.parse(validDeploymentMessageJson, "raspberrypi-01").sourceCode, "print('hello')\n");

  const ApplicationDeploymentMessageParser parserBelowLimit(14U);
  expectParsingError(parserBelowLimit, validDeploymentMessageJson,
                     "Deployment field 'size_bytes' has an invalid byte count");
}

TEST(ApplicationDeploymentMessageParserTest, RejectsDecodedSourceOverTheLimitEvenWhenDeclaredSizeFits) {
  const ApplicationDeploymentMessageParser deploymentMessageParser(14U);
  std::string                              wrongDeclaredSize(validDeploymentMessageJson);
  wrongDeclaredSize.replace(wrongDeclaredSize.find("\"size_bytes\":15"), 15U, "\"size_bytes\":14");

  expectParsingError(deploymentMessageParser, wrongDeclaredSize,
                     "Decoded deployment source is larger than the allowed limit");
}

TEST(ApplicationDeploymentMessageParserTest, RejectsAZeroSourceSizeLimit) {
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
