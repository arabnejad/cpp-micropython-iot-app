#pragma once

#include <memory>
#include <string>
#include <string_view>

struct cJSON;

namespace iot {
namespace internal {

struct CJsonObjectDeleter {
  void operator()(cJSON *jsonObject) const noexcept;
};

struct CJsonTextDeleter {
  void operator()(char *jsonText) const noexcept;
};

/* cJSON_Delete frees an object; cJSON_free frees text returned by cJSON_Print. */
using UniqueCJsonObject = std::unique_ptr<cJSON, CJsonObjectDeleter>;
using UniqueCJsonText   = std::unique_ptr<char, CJsonTextDeleter>;

/* Reads exactly one JSON object and includes documentName in parsing errors. */
UniqueCJsonObject parseCompleteJsonObject(const std::string &jsonText, std::string_view documentName);

/*
 * Shared checks for fields read from application JSON. The document name is
 * included in error messages, for example "Application metadata" or
 * "Deployment message".
 */
const cJSON *findUniqueJsonField(const cJSON *jsonObject, std::string_view fieldName, std::string_view documentName);

std::string requireNonEmptyJsonStringField(const cJSON *jsonObject, std::string_view fieldName,
                                           std::string_view documentName);

} // namespace internal
} // namespace iot
