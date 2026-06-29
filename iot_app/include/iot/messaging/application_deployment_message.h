#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace iot {
namespace messaging {

/* Validated application details kept after an MQTT install message is parsed. */
struct ApplicationDeploymentRequest {
  std::string transferId;
  std::string applicationId;
  std::string applicationName;
  std::string entryPoint;
  std::string sourceCode;
};

/* Deployment progress or final result returned to the sender. */
struct ApplicationDeploymentStatus {
  std::string transferId;
  std::string deploymentState;
  std::string applicationId;
  std::string message;
};

/*
 * Reads the JSON message created by iot_app_sender/send_app.py and checks
 * its fields, source size, and hash.
 */
class ApplicationDeploymentMessageParser {
public:
  explicit ApplicationDeploymentMessageParser(std::size_t maximumSourceSizeInBytes);

  /*
   * Parses one request. Invalid fields, sizes, hashes, and device IDs cause an
   * exception.
   */
  ApplicationDeploymentRequest parse(const std::string &messagePayload, const std::string &expectedDeviceId) const;

  /*
   * Reads the transfer ID without validating the complete request.
   *
   * This is used to report a rejection when the rest of the message is bad.
   */
  std::optional<std::string> tryReadTransferId(const std::string &messagePayload) const noexcept;

  /* Converts a deployment status into the JSON expected by the sender. */
  static std::string serializeStatusPayload(const ApplicationDeploymentStatus &deploymentStatus);

private:
  std::size_t m_maximumSourceSizeInBytes{0};
};

} // namespace messaging
} // namespace iot
