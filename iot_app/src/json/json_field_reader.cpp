#include "json/json_field_reader.h"

#include <cjson/cJSON.h>

#include <stdexcept>
#include <string>

namespace iot {
namespace internal {

void CJsonObjectDeleter::operator()(cJSON *jsonObject) const noexcept {
  cJSON_Delete(jsonObject);
}

void CJsonTextDeleter::operator()(char *jsonText) const noexcept {
  cJSON_free(jsonText);
}

UniqueCJsonObject parseCompleteJsonObject(const std::string &jsonText, std::string_view documentName) {
  if (jsonText.empty() || jsonText.find('\0') != std::string::npos) {
    throw std::runtime_error(std::string(documentName) + " is empty or contains a null byte");
  }

  const char       *parseEnd = nullptr;
  UniqueCJsonObject jsonObject{cJSON_ParseWithLengthOpts(jsonText.c_str(), jsonText.size() + 1U, &parseEnd, 1)};
  if (!jsonObject || !cJSON_IsObject(jsonObject.get())) {
    throw std::runtime_error(std::string(documentName) + " is not one complete JSON object");
  }
  return jsonObject;
}

const cJSON *findUniqueJsonField(const cJSON *jsonObject, std::string_view fieldName, std::string_view documentName) {
  const cJSON *matchingField = nullptr;
  for (const cJSON *field = jsonObject == nullptr ? nullptr : jsonObject->child; field != nullptr;
       field              = field->next) {
    if (field->string != nullptr && fieldName == field->string) {
      if (matchingField != nullptr) {
        throw std::runtime_error(std::string(documentName) + " contains the field '" + std::string(fieldName) +
                                 "' more than once");
      }
      matchingField = field;
    }
  }

  if (matchingField == nullptr) {
    throw std::runtime_error(std::string(documentName) + " is missing the field '" + std::string(fieldName) + "'");
  }
  return matchingField;
}

std::string requireNonEmptyJsonStringField(const cJSON *jsonObject, std::string_view fieldName,
                                           std::string_view documentName) {
  const cJSON *field = findUniqueJsonField(jsonObject, fieldName, documentName);
  if (!cJSON_IsString(field) || field->valuestring == nullptr || field->valuestring[0] == '\0') {
    throw std::runtime_error(std::string(documentName) + " field '" + std::string(fieldName) +
                             "' must be a non-empty string");
  }
  return field->valuestring;
}

} // namespace internal
} // namespace iot
