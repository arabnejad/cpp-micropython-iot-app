#pragma once

#include <string>
#include <string_view>

struct cJSON;

namespace iot {
namespace internal {

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
