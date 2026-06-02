#pragma once

#include <string>

namespace iot {
namespace python {

/** The three metadata values required for every Python application. */
struct ApplicationMetadata {
  std::string applicationId;
  std::string applicationName;
  std::string entryPoint;
};

/** Reads application metadata from JSON and checks all required fields. */
ApplicationMetadata parseApplicationMetadata(const std::string &metadataJson);

/** Checks application metadata values that have already been read from JSON. */
void validateApplicationMetadata(const ApplicationMetadata &metadata);

} // namespace python
} // namespace iot
