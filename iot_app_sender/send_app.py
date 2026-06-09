#!/usr/bin/env python3
"""Send a single-file MicroPython application to an IoT App device."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
import re
import sys
from threading import Event
from typing import Any, Optional
from uuid import uuid4

import paho.mqtt.client as mqtt


# These limits are part of the current sender design, not choices that need to
# be repeated in every device configuration.
MQTT_KEEP_ALIVE_SECONDS = 60
MQTT_CONNECTION_TIMEOUT_SECONDS = 10
DEVICE_ACKNOWLEDGEMENT_TIMEOUT_SECONDS = 30
MAXIMUM_DEPLOYMENT_MESSAGE_SIZE_BYTES = 1_000_000

FINAL_DEVICE_STATUSES = {
    "started",
    "rejected",
    "failed",
}
SUCCESS_DEVICE_STATUS = "started"
APPLICATION_ID_PATTERN = re.compile(r"^[A-Za-z0-9._-]+$")


class SenderError(RuntimeError):
    """Describes a configuration, application, connection, or deployment error."""


@dataclass(frozen=True)
class MqttSettings:
    """Contains the MQTT broker address selected by the user."""

    broker_host: str
    broker_port: int


@dataclass(frozen=True)
class SenderConfiguration:
    """Contains the target device and the application directory to send."""

    device_id: str
    mqtt: MqttSettings
    application_directory: Path


@dataclass
class DeploymentState:
    """Shares MQTT callback results with the command-line thread."""

    status_topic: str
    transfer_id: str
    connected: Event = field(default_factory=Event)
    subscribed: Event = field(default_factory=Event)
    final_response_received: Event = field(default_factory=Event)
    connection_error: Optional[str] = None
    subscription_error: Optional[str] = None
    final_response: Optional[dict[str, Any]] = None


@dataclass(frozen=True)
class PreparedDeployment:
    """Contains one validated application and the MQTT message ready to send."""

    payload: bytes
    install_topic: str
    status_topic: str
    application_metadata: dict[str, str]
    entry_point_path: Path


def require_object(value: Any, description: str) -> dict[str, Any]:
    """Returns a JSON object or explains which configuration section is wrong."""

    if not isinstance(value, dict):
        raise SenderError(f"{description} must be a JSON object")
    return value


def require_non_empty_string(container: dict[str, Any], name: str, description: str) -> str:
    """Reads one required text setting and rejects missing or blank text."""

    value = container.get(name)
    if not isinstance(value, str) or not value.strip():
        raise SenderError(f"{description}.{name} must be a non-empty string")
    return value.strip()


def require_integer_in_range(
    container: dict[str, Any],
    name: str,
    description: str,
    minimum: int,
    maximum: int,
) -> int:
    """Reads an integer setting and checks that it is inside the allowed range."""

    value = container.get(name)
    if isinstance(value, bool) or not isinstance(value, int):
        raise SenderError(f"{description}.{name} must be an integer")
    if value < minimum or value > maximum:
        raise SenderError(
            f"{description}.{name} must be between {minimum:g} and {maximum:g}"
        )
    return value


def resolve_application_directory(
    config_directory: Path, configured_path: str
) -> Path:
    """Resolves the configured application directory."""

    path = Path(configured_path).expanduser()
    if not path.is_absolute():
        path = config_directory / path
    try:
        resolved_path = path.resolve(strict=True)
    except OSError as error:
        raise SenderError(
            f"Configured application directory is not available: {path}: {error}"
        ) from error
    if not resolved_path.is_dir():
        raise SenderError(
            f"Configured application path is not a directory: {resolved_path}"
        )
    return resolved_path


def load_sender_configuration(config_path: Path) -> SenderConfiguration:
    """Loads sender settings and resolves its application paths."""

    try:
        resolved_config_path = config_path.expanduser().resolve(strict=True)
        raw_configuration = json.loads(resolved_config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SenderError(f"Could not read sender configuration {config_path}: {error}") from error

    configuration = require_object(raw_configuration, "sender configuration")
    device_id = require_non_empty_string(configuration, "device_id", "sender configuration")
    if not APPLICATION_ID_PATTERN.fullmatch(device_id):
        raise SenderError("device_id may contain only letters, numbers, '.', '-', and '_'")

    mqtt_config = require_object(configuration.get("mqtt"), "mqtt")
    application_config = require_object(configuration.get("application"), "application")
    config_directory = resolved_config_path.parent

    broker_port = require_integer_in_range(
        mqtt_config, "broker_port", "mqtt", 1, 65535
    )
    mqtt_settings = MqttSettings(
        broker_host=require_non_empty_string(mqtt_config, "broker_host", "mqtt"),
        broker_port=broker_port,
    )

    application_directory = resolve_application_directory(
        config_directory,
        require_non_empty_string(application_config, "directory", "application"),
    )
    return SenderConfiguration(device_id, mqtt_settings, application_directory)


def load_and_validate_application_metadata(
    application_directory: Path,
) -> dict[str, str]:
    """Reads the three fields understood by the current C++ application loader."""

    metadata_path = application_directory / "app.json"
    try:
        raw_metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SenderError(
            f"Could not read application metadata {metadata_path}: {error}"
        ) from error

    metadata = require_object(raw_metadata, "application metadata")
    validated_metadata = {
        name: require_non_empty_string(metadata, name, "application metadata")
        for name in ("id", "name", "entry_point")
    }
    if not APPLICATION_ID_PATTERN.fullmatch(validated_metadata["id"]):
        raise SenderError("application id may contain only letters, numbers, '.', '-', and '_'")

    entry_point = PurePosixPath(validated_metadata["entry_point"])
    if entry_point.is_absolute() or ".." in entry_point.parts:
        raise SenderError("application entry_point must stay inside the application directory")
    if entry_point.suffix != ".py":
        raise SenderError("the single-file application entry_point must be a .py file")
    return validated_metadata


def resolve_application_entry_point(
    application_directory: Path, application_metadata: dict[str, str]
) -> Path:
    """Finds the Python file named by app.json and keeps it inside the app."""

    relative_entry_point = PurePosixPath(application_metadata["entry_point"])
    entry_point_path = application_directory.joinpath(*relative_entry_point.parts)
    try:
        resolved_entry_point = entry_point_path.resolve(strict=True)
        resolved_entry_point.relative_to(application_directory)
    except (OSError, ValueError) as error:
        raise SenderError(
            "Application entry point is missing or resolves outside its directory: "
            f"{entry_point_path}"
        ) from error
    if not resolved_entry_point.is_file():
        raise SenderError(
            f"Application entry point is not a regular file: {resolved_entry_point}"
        )
    return resolved_entry_point


def build_deployment_message(
    configuration: SenderConfiguration,
    transfer_id: str,
) -> PreparedDeployment:
    """Builds one MQTT message containing application metadata and Python source."""

    application_metadata = load_and_validate_application_metadata(
        configuration.application_directory
    )
    entry_point_path = resolve_application_entry_point(
        configuration.application_directory, application_metadata
    )
    try:
        source_bytes = entry_point_path.read_bytes()
        source_bytes.decode("utf-8")
    except OSError as error:
        raise SenderError(f"Could not read Python source: {error}") from error
    except UnicodeDecodeError as error:
        raise SenderError("main Python source must be valid UTF-8 text") from error
    if not source_bytes:
        raise SenderError("main Python source is empty")
    if b"\0" in source_bytes:
        raise SenderError("main Python source contains a null byte")

    install_topic = f"iot/devices/{configuration.device_id}/applications/install"
    status_topic = (
        f"iot/devices/{configuration.device_id}/applications/status/{transfer_id}"
    )
    message = {
        "message_type": "install_single_file_application",
        "transfer_id": transfer_id,
        "device_id": configuration.device_id,
        "application": application_metadata,
        "source": {
            "encoding": "base64",
            "size_bytes": len(source_bytes),
            "sha256": hashlib.sha256(source_bytes).hexdigest(),
            "content": base64.b64encode(source_bytes).decode("ascii"),
        },
    }
    payload = json.dumps(message, separators=(",", ":"), sort_keys=True).encode("utf-8")
    if len(payload) > MAXIMUM_DEPLOYMENT_MESSAGE_SIZE_BYTES:
        raise SenderError(
            "Deployment message is %d bytes, which exceeds the sender's %d-byte limit. "
            "Use a smaller app or the future archive download/chunk protocol."
            % (len(payload), MAXIMUM_DEPLOYMENT_MESSAGE_SIZE_BYTES)
        )
    return PreparedDeployment(
        payload=payload,
        install_topic=install_topic,
        status_topic=status_topic,
        application_metadata=application_metadata,
        entry_point_path=entry_point_path,
    )


def create_mqtt_client(configuration: SenderConfiguration, state: DeploymentState):
    """Creates the MQTT 5 client and connects its callbacks to deployment state."""

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"iot-app-sender-{uuid4().hex[:12]}",
        protocol=mqtt.MQTTv5,
    )

    def on_connect(client, _userdata, _flags, reason_code, _properties):
        if reason_code.is_failure:
            state.connection_error = f"MQTT broker rejected the connection: {reason_code}"
            state.connected.set()
            return
        state.connected.set()
        result, _message_id = client.subscribe(state.status_topic, qos=1)
        if result != mqtt.MQTT_ERR_SUCCESS:
            state.subscription_error = f"Could not subscribe to {state.status_topic}"
            state.subscribed.set()

    def on_subscribe(_client, _userdata, _message_id, reason_codes, _properties):
        if any(reason_code.is_failure for reason_code in reason_codes):
            state.subscription_error = "MQTT broker rejected the deployment status subscription"
        state.subscribed.set()

    def on_message(_client, _userdata, message):
        try:
            response = json.loads(message.payload.decode("utf-8"))
            if not isinstance(response, dict) or response.get("transfer_id") != state.transfer_id:
                return
            status = response.get("status")
            print(f"Device status: {status}: {response.get('message', '')}".rstrip())
            if status in FINAL_DEVICE_STATUSES:
                state.final_response = response
                state.final_response_received.set()
        except (UnicodeDecodeError, json.JSONDecodeError):
            print("Warning: ignored an invalid deployment status message", file=sys.stderr)

    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message

    return client


def send_application(configuration: SenderConfiguration, wait_for_device: bool) -> int:
    """Publishes the deployment request and optionally waits for the Pi result."""

    transfer_id = uuid4().hex
    deployment = build_deployment_message(configuration, transfer_id)
    state = DeploymentState(status_topic=deployment.status_topic, transfer_id=transfer_id)
    client = create_mqtt_client(configuration, state)
    mqtt_settings = configuration.mqtt

    print(f"Application: {configuration.application_directory}")
    print(f"Python source: {deployment.entry_point_path}")
    print(f"MQTT broker: {mqtt_settings.broker_host}:{mqtt_settings.broker_port}")
    print(f"Install topic: {deployment.install_topic}")
    print(f"Transfer ID: {transfer_id}")
    print(f"Message size: {len(deployment.payload)} bytes")

    try:
        client.connect(
            mqtt_settings.broker_host,
            mqtt_settings.broker_port,
            MQTT_KEEP_ALIVE_SECONDS,
        )
        client.loop_start()
        if not state.connected.wait(MQTT_CONNECTION_TIMEOUT_SECONDS):
            raise SenderError("Timed out while connecting to the MQTT broker")
        if state.connection_error:
            raise SenderError(state.connection_error)
        if not state.subscribed.wait(MQTT_CONNECTION_TIMEOUT_SECONDS):
            raise SenderError("Timed out while subscribing to the deployment status topic")
        if state.subscription_error:
            raise SenderError(state.subscription_error)

        publish_result = client.publish(
            deployment.install_topic,
            payload=deployment.payload,
            qos=1,
            retain=False,
        )
        publish_result.wait_for_publish(timeout=MQTT_CONNECTION_TIMEOUT_SECONDS)
        if not publish_result.is_published():
            raise SenderError("Timed out before the broker acknowledged the deployment message")
        print("The MQTT broker acknowledged the deployment message.")

        if not wait_for_device:
            print("Not waiting for the Raspberry Pi application result (--no-wait).")
            return 0
        if not state.final_response_received.wait(DEVICE_ACKNOWLEDGEMENT_TIMEOUT_SECONDS):
            raise SenderError(
                "The broker received the message, but the Raspberry Pi did not send a final "
                "deployment status before the timeout"
            )
        final_response = state.final_response
        return 0 if final_response and final_response.get("status") == SUCCESS_DEVICE_STATUS else 2
    finally:
        client.disconnect()
        client.loop_stop()


def parse_arguments() -> argparse.Namespace:
    """Reads the sender configuration path and development options."""

    parser = argparse.ArgumentParser(
        description="Send one MicroPython application to an IoT App device through MQTT."
    )
    parser.add_argument(
        "configuration",
        nargs="?",
        type=Path,
        default=Path(__file__).with_name("sender_config.json"),
        help="sender JSON file (default: iot_app_sender/sender_config.json)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="validate and build the message without connecting to MQTT",
    )
    parser.add_argument(
        "--no-wait",
        action="store_true",
        help="stop after broker acknowledgement instead of waiting for the Pi result",
    )
    return parser.parse_args()


def main() -> int:
    """Runs the command-line sender and converts expected errors into readable messages."""

    arguments = parse_arguments()
    try:
        configuration = load_sender_configuration(arguments.configuration)
        if arguments.dry_run:
            transfer_id = uuid4().hex
            deployment = build_deployment_message(configuration, transfer_id)
            print("Deployment message is valid.")
            print("Application: %s" % deployment.application_metadata["name"])
            print(f"Install topic: {deployment.install_topic}")
            print(f"Expected status topic: {deployment.status_topic}")
            print(f"Message size: {len(deployment.payload)} bytes")
            return 0
        return send_application(configuration, wait_for_device=not arguments.no_wait)
    except SenderError as error:
        print(f"iot_app_sender failed: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"iot_app_sender failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
