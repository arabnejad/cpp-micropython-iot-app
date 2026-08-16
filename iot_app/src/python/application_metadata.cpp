#include "iot/python/application_metadata.h"
#include "iot/python/path_validation.h"

#include <cjson/cJSON.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace iot {
namespace python {
namespace {

constexpr std::size_t maximumApplicationIdLength = 128U;

struct CJsonObjectDeleter {
  void operator()(cJSON *jsonObject) const noexcept {
    cJSON_Delete(jsonObject);
  }
};

using UniqueCJsonObject = std::unique_ptr<cJSON, CJsonObjectDeleter>;

const cJSON *findUniqueField(const cJSON *jsonObject, const char *fieldName) {
  const cJSON *matchingField = nullptr;
  for (const cJSON *field = jsonObject == nullptr ? nullptr : jsonObject->child; field != nullptr;
       field              = field->next) {
    if (field->string != nullptr && std::string_view(field->string) == fieldName) {
      if (matchingField != nullptr) {
        throw std::runtime_error("Application metadata contains the field '" + std::string(fieldName) +
                                 "' more than once");
      }
      matchingField = field;
    }
  }
  if (matchingField == nullptr) {
    throw std::runtime_error("Application metadata is missing the field '" + std::string(fieldName) + "'");
  }
  return matchingField;
}

std::string requireStringField(const cJSON *jsonObject, const char *fieldName) {
  const cJSON *field = findUniqueField(jsonObject, fieldName);
  if (!cJSON_IsString(field) || field->valuestring == nullptr || field->valuestring[0] == '\0') {
    throw std::runtime_error("Application metadata field '" + std::string(fieldName) + "' must be a non-empty string");
  }
  return field->valuestring;
}

bool isValidApplicationId(std::string_view applicationId) {
  if (applicationId.empty() || applicationId.size() > maximumApplicationIdLength) {
    return false;
  }
  for (const char character : applicationId) {
    const bool isLetter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    const bool isNumber = character >= '0' && character <= '9';
    if (!isLetter && !isNumber && character != '.' && character != '-' && character != '_') {
      return false;
    }
  }
  return true;
}

} // namespace

void validateApplicationMetadata(const ApplicationMetadata &metadata) {
  if (!isValidApplicationId(metadata.applicationId)) {
    throw std::runtime_error(
        "Application id must be 1 to 128 characters and contain only letters, numbers, '.', '-', and '_'");
  }
  if (metadata.applicationName.empty()) {
    throw std::runtime_error("Application name must not be empty");
  }
  const std::filesystem::path entryPointPath(metadata.entryPoint);
  if (!isSafeRelativePath(entryPointPath) || entryPointPath.extension() != ".py") {
    throw std::runtime_error("Application entry_point must be a relative .py path inside the application directory");
  }
}

ApplicationMetadata parseApplicationMetadata(const std::string &metadataJson) {
  if (metadataJson.empty() || metadataJson.find('\0') != std::string::npos) {
    throw std::runtime_error("Application metadata is empty or contains a null byte");
  }

  const char       *parseEnd = nullptr;
  UniqueCJsonObject metadataObject{
      cJSON_ParseWithLengthOpts(metadataJson.c_str(), metadataJson.size() + 1U, &parseEnd, 1)};
  if (!metadataObject || !cJSON_IsObject(metadataObject.get())) {
    throw std::runtime_error("Application metadata is not one complete JSON object");
  }

  ApplicationMetadata metadata{requireStringField(metadataObject.get(), "id"),
                               requireStringField(metadataObject.get(), "name"),
                               requireStringField(metadataObject.get(), "entry_point")};
  validateApplicationMetadata(metadata);
  return metadata;
}

} // namespace python
} // namespace iot
