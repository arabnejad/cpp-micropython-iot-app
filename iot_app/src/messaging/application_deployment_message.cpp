#include "iot/messaging/application_deployment_message.h"

#include "iot/python/application_metadata.h"

#include <cjson/cJSON.h>
#include <openssl/evp.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace iot {
namespace messaging {
namespace {

constexpr std::size_t maximumIdentifierLength = 128U;

struct CJsonObjectDeleter {
  void operator()(cJSON *jsonObject) const noexcept {
    cJSON_Delete(jsonObject);
  }
};

using UniqueCJsonObject = std::unique_ptr<cJSON, CJsonObjectDeleter>;

bool isSafeIdentifier(std::string_view identifier) {
  if (identifier.empty() || identifier.size() > maximumIdentifierLength) {
    return false;
  }
  for (const char character : identifier) {
    const bool isLetter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    const bool isNumber = character >= '0' && character <= '9';
    if (!isLetter && !isNumber && character != '.' && character != '-' && character != '_') {
      return false;
    }
  }
  return true;
}

const cJSON *findUniqueField(const cJSON *jsonObject, const char *fieldName) {
  const cJSON *matchingField = nullptr;
  for (const cJSON *field = jsonObject == nullptr ? nullptr : jsonObject->child; field != nullptr;
       field              = field->next) {
    if (field->string != nullptr && std::string_view(field->string) == fieldName) {
      if (matchingField != nullptr) {
        throw std::runtime_error("Deployment message contains the field '" + std::string(fieldName) +
                                 "' more than once");
      }
      matchingField = field;
    }
  }
  if (matchingField == nullptr) {
    throw std::runtime_error("Deployment message is missing the field '" + std::string(fieldName) + "'");
  }
  return matchingField;
}

const cJSON *requireObjectField(const cJSON *jsonObject, const char *fieldName) {
  const cJSON *field = findUniqueField(jsonObject, fieldName);
  if (!cJSON_IsObject(field)) {
    throw std::runtime_error("Deployment field '" + std::string(fieldName) + "' must be a JSON object");
  }
  return field;
}

std::string requireStringField(const cJSON *jsonObject, const char *fieldName) {
  const cJSON *field = findUniqueField(jsonObject, fieldName);
  if (!cJSON_IsString(field) || field->valuestring == nullptr || field->valuestring[0] == '\0') {
    throw std::runtime_error("Deployment field '" + std::string(fieldName) + "' must be a non-empty string");
  }
  return field->valuestring;
}

std::size_t requireSizeField(const cJSON *jsonObject, const char *fieldName, std::size_t maximumValue) {
  const cJSON *field = findUniqueField(jsonObject, fieldName);
  if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble) || field->valuedouble < 0.0 ||
      std::floor(field->valuedouble) != field->valuedouble || field->valuedouble > static_cast<double>(maximumValue)) {
    throw std::runtime_error("Deployment field '" + std::string(fieldName) + "' has an invalid byte count");
  }
  return static_cast<std::size_t>(field->valuedouble);
}

UniqueCJsonObject parseJson(const std::string &messagePayload) {
  if (messagePayload.empty() || messagePayload.find('\0') != std::string::npos) {
    throw std::runtime_error("Deployment message is empty or contains a null byte");
  }

  const char       *parseEnd = nullptr;
  UniqueCJsonObject jsonObject{
      cJSON_ParseWithLengthOpts(messagePayload.c_str(), messagePayload.size() + 1U, &parseEnd, 1)};
  if (!jsonObject || !cJSON_IsObject(jsonObject.get())) {
    throw std::runtime_error("Deployment message is not one complete JSON object");
  }
  return jsonObject;
}

std::string decodeBase64(const std::string &encodedText, std::size_t maximumDecodedSize) {
  if (encodedText.empty() || encodedText.size() % 4U != 0U) {
    throw std::runtime_error("Deployment source is not valid Base64 text");
  }

  std::size_t paddingBytes = 0U;
  if (encodedText.back() == '=') {
    ++paddingBytes;
  }
  if (encodedText.size() >= 2U && encodedText[encodedText.size() - 2U] == '=') {
    ++paddingBytes;
  }

  for (std::size_t index = 0U; index < encodedText.size(); ++index) {
    const char character         = encodedText[index];
    const bool isLetter          = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    const bool isNumber          = character >= '0' && character <= '9';
    const bool isBase64Character = isLetter || isNumber || character == '+' || character == '/';
    const bool isAllowedPadding  = character == '=' && index >= encodedText.size() - paddingBytes;
    if (!isBase64Character && !isAllowedPadding) {
      throw std::runtime_error("Deployment source contains an invalid Base64 character");
    }
  }

  const std::size_t maximumOutputSize = (encodedText.size() / 4U) * 3U;
  if (maximumOutputSize < paddingBytes || maximumOutputSize - paddingBytes > maximumDecodedSize ||
      encodedText.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("Decoded deployment source is larger than the allowed limit");
  }

  std::string decodedText(maximumOutputSize, '\0');
  const int   decodedSize = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(decodedText.data()),
                                            reinterpret_cast<const unsigned char *>(encodedText.data()),
                                            static_cast<int>(encodedText.size()));
  if (decodedSize < 0 || static_cast<std::size_t>(decodedSize) < paddingBytes) {
    throw std::runtime_error("Deployment source could not be decoded from Base64");
  }
  decodedText.resize(static_cast<std::size_t>(decodedSize) - paddingBytes);
  return decodedText;
}

std::string calculateSha256(const std::string &sourceCode) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digestBytes{};
  unsigned int                               digestSize = 0U;
  if (EVP_Digest(sourceCode.data(), sourceCode.size(), digestBytes.data(), &digestSize, EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("OpenSSL could not calculate the source SHA-256 digest");
  }

  std::ostringstream hexadecimalDigest;
  hexadecimalDigest << std::hex << std::setfill('0');
  for (unsigned int index = 0U; index < digestSize; ++index) {
    hexadecimalDigest << std::setw(2) << static_cast<unsigned int>(digestBytes[index]);
  }
  return hexadecimalDigest.str();
}

void addJsonString(cJSON *jsonObject, const char *fieldName, const std::string &value) {
  if (cJSON_AddStringToObject(jsonObject, fieldName, value.c_str()) == nullptr) {
    throw std::runtime_error("Could not allocate deployment status JSON");
  }
}

} // namespace

ApplicationDeploymentMessageParser::ApplicationDeploymentMessageParser(std::size_t maximumSourceSizeInBytes)
    : m_maximumSourceSizeInBytes(maximumSourceSizeInBytes) {
  if (m_maximumSourceSizeInBytes == 0U) {
    throw std::invalid_argument("Deployment parser requires a non-zero Python source limit");
  }
}

ApplicationDeploymentRequest ApplicationDeploymentMessageParser::parse(const std::string &messagePayload,
                                                                       const std::string &expectedDeviceId) const {
  const auto jsonObject = parseJson(messagePayload);

  if (requireStringField(jsonObject.get(), "message_type") != "install_single_file_application") {
    throw std::runtime_error("Deployment message_type is not supported");
  }

  ApplicationDeploymentRequest deploymentRequest;
  deploymentRequest.transferId       = requireStringField(jsonObject.get(), "transfer_id");
  const std::string receivedDeviceId = requireStringField(jsonObject.get(), "device_id");
  if (!isSafeIdentifier(deploymentRequest.transferId)) {
    throw std::runtime_error("Deployment transfer_id contains unsupported characters");
  }
  if (receivedDeviceId != expectedDeviceId) {
    throw std::runtime_error("Deployment message is intended for a different device");
  }

  const cJSON                      *applicationObject = requireObjectField(jsonObject.get(), "application");
  const python::ApplicationMetadata metadata{requireStringField(applicationObject, "id"),
                                             requireStringField(applicationObject, "name"),
                                             requireStringField(applicationObject, "entry_point")};
  python::validateApplicationMetadata(metadata);
  deploymentRequest.applicationId   = metadata.applicationId;
  deploymentRequest.applicationName = metadata.applicationName;
  deploymentRequest.entryPoint      = metadata.entryPoint;

  const cJSON *source = requireObjectField(jsonObject.get(), "source");
  if (requireStringField(source, "encoding") != "base64") {
    throw std::runtime_error("Deployment source encoding is not supported");
  }

  const std::size_t declaredSourceSize = requireSizeField(source, "size_bytes", m_maximumSourceSizeInBytes);
  const std::string declaredSourceHash = requireStringField(source, "sha256");
  deploymentRequest.sourceCode = decodeBase64(requireStringField(source, "content"), m_maximumSourceSizeInBytes);
  if (deploymentRequest.sourceCode.empty() || deploymentRequest.sourceCode.find('\0') != std::string::npos) {
    throw std::runtime_error("Decoded Python source is empty or contains a null byte");
  }
  if (deploymentRequest.sourceCode.size() != declaredSourceSize) {
    throw std::runtime_error("Decoded Python source size does not match size_bytes");
  }
  if (declaredSourceHash.size() != 64U || calculateSha256(deploymentRequest.sourceCode) != declaredSourceHash) {
    throw std::runtime_error("Decoded Python source does not match its SHA-256 hash");
  }
  return deploymentRequest;
}

std::optional<std::string>
ApplicationDeploymentMessageParser::tryReadTransferId(const std::string &messagePayload) const noexcept {
  try {
    const auto        jsonObject = parseJson(messagePayload);
    const std::string transferId = requireStringField(jsonObject.get(), "transfer_id");
    if (isSafeIdentifier(transferId)) {
      return transferId;
    }
  } catch (...) {
  }
  return std::nullopt;
}

std::string
ApplicationDeploymentMessageParser::serializeStatusPayload(const ApplicationDeploymentStatus &deploymentStatus) {
  UniqueCJsonObject jsonObject{cJSON_CreateObject()};
  if (!jsonObject) {
    throw std::runtime_error("Could not allocate deployment status JSON");
  }
  addJsonString(jsonObject.get(), "transfer_id", deploymentStatus.transferId);
  addJsonString(jsonObject.get(), "status", deploymentStatus.deploymentState);
  addJsonString(jsonObject.get(), "application_id", deploymentStatus.applicationId);
  addJsonString(jsonObject.get(), "message", deploymentStatus.message);

  char *serializedJson = cJSON_PrintUnformatted(jsonObject.get());
  if (serializedJson == nullptr) {
    throw std::runtime_error("Could not serialize deployment status JSON");
  }
  std::string statusPayload(serializedJson);
  cJSON_free(serializedJson);
  return statusPayload;
}

} // namespace messaging
} // namespace iot
