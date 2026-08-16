#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace iot {
namespace messaging {

/** Application details and Python source read from one MQTT install message. */
struct ApplicationDeploymentRequest {
  std::string transferId;
  std::string deviceId;
  std::string applicationId;
  std::string applicationName;
  std::string entryPoint;
  std::string sourceCode;
};

/** Progress or final result sent back to the Ubuntu sender. */
struct ApplicationDeploymentStatus {
  std::string transferId;
  std::string deploymentState;
  std::string applicationId;
  std::string message;
};

/**
 * Reads the JSON message created by `iot_app_sender/send_app.py` and checks
 * that it is safe and complete.
 */
class ApplicationDeploymentMessageParser {
public:
  explicit ApplicationDeploymentMessageParser(std::size_t maximumSourceSizeInBytes);

  /**
   * Reads one deployment request. It throws if a field, byte count, SHA-256
   * hash, or target device ID is wrong.
   */
  ApplicationDeploymentRequest parse(const std::string &messagePayload, const std::string &expectedDeviceId) const;

  /**
   * Tries to recover the transfer ID from a message that failed validation.
   *
   * The ID lets the device calculate the correct status topic even when the
   * rest of the message cannot be used.
   */
  std::optional<std::string> tryReadTransferId(const std::string &messagePayload) const noexcept;

  /** Converts a deployment status into the JSON expected by the sender. */
  static std::string serializeStatusPayload(const ApplicationDeploymentStatus &deploymentStatus);

private:
  std::size_t maximumSourceSizeInBytes_{0};
};

} // namespace messaging
} // namespace iot
