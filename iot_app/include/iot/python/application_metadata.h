#pragma once

#include <string>

namespace iot {
namespace python {

/* Required fields from app.json. */
struct ApplicationMetadata {
  std::string applicationId;
  std::string applicationName;
  std::string entryPoint;
};

/* Parses and validates app.json content. */
ApplicationMetadata parseApplicationMetadata(const std::string &metadataJson);

/* Validates application metadata that has already been read from JSON. */
void validateApplicationMetadata(const ApplicationMetadata &metadata);

} // namespace python
} // namespace iot
