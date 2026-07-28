#include "json/json_field_reader.h"

#include <cjson/cJSON.h>

#include <stdexcept>
#include <string>

namespace iot {
namespace internal {

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
