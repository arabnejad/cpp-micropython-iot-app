# IoT App Sender

This Ubuntu tool sends a single-file MicroPython application to a Raspberry Pi
through MQTT. The current format is deliberately small. A later version may
grow into an `.iotapp` package sender with support for several files and assets.

The sender publishes to an MQTT broker. With the example configuration,
Mosquitto runs on the Raspberry Pi at `rspi-iot-app.local`; the C++ receiver
inside `iot_app` subscribes to that same broker.

```text
Ubuntu                                      Raspberry Pi
iot_app_sender                              Mosquitto broker
      |                                            |
      +---- install request, MQTT QoS 1 ---------->|
                                                   |
                                            iot_app subscriber
                                                   |
      |<------- deployment status, MQTT QoS 1 -----+
```

An acknowledgement from the broker only says that it received the MQTT
message. It does not say that the application started. Each deployment has its
own transfer ID and status topic, and the sender waits for `iot_app` to report
the result.

The C++ receiver starts the shipped default app before subscribing. A valid
external app replaces the current Python session without restarting the C++
process. If external startup raises an exception, the device reports the
failure and shows its native emergency screen. The default app runs again only
after `iot_app` restarts.

The `sample_applications` catalog contains clocks, system displays, gamepad
diagnostics, menus, counters, a countdown, and failure tests for startup and
scheduled failures. Each application has its own README. The default sender
configuration selects the simple clock, which updates one existing text box
every second with `scheduler.every()`.

## Application and sender configuration

Each application keeps its metadata in `app.json`:

```json
{
  "id": "ubuntu-clock",
  "name": "Ubuntu clock app",
  "entry_point": "main.py"
}
```

Connection settings and the local application directory belong in a separate
`sender_config.json`:

```json
{
  "device_id": "raspberrypi-01",
  "mqtt": {
    "broker_host": "rspi-iot-app.local",
    "broker_port": 1883
  },
  "application": {
    "directory": "sample_applications/clock"
  }
}
```

The sender reads `app.json` from this directory and uses its `entry_point` to
find the Python source. The directory is resolved relative to
`sender_config.json`, not the terminal's current directory. The development
sender connects to the anonymous MQTT listener described below. The untracked
name `sender_config.json` is already included in the repository `.gitignore`.

Stable operational limits are constants in `send_app.py`:

```text
MQTT keep alive:                60 seconds
MQTT connection timeout:        10 seconds
Device acknowledgement timeout: 30 seconds
Maximum deployment message:     1,000,000 bytes
```

The fixed message limit remains below IoT App's 1,048,576-byte MQTT limit. The
decoded Python entry point also has a separate 524,288-byte limit on the
Raspberry Pi.

To send another sample, change only the application directory. For example:

```json
{
  "directory": "sample_applications/gamepad_diagnostics"
}
```

To resend the same default dashboard that is installed with IoT App, use:

```json
{
  "directory": "../iot_app/default_python_application"
}
```

The authoritative default application remains under `iot_app`, so CMake,
Buildroot, Yocto, and manual MQTT dashboard restoration all use the same
Python source.

See [`sample_applications/README.md`](sample_applications/README.md) for the
complete catalog, timer intervals, and hardware requirements.

`broker_host` means the machine running Mosquitto. The example uses the
Raspberry Pi's `rspi-iot-app.local` name because Mosquitto runs on the Pi. If
the broker runs on Ubuntu, use a hostname or address that the sender and the Pi
can both reach.

## Ubuntu setup

Create a local environment and install the MQTT client:

```bash
cd iot_app_sender
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
cp sender_config.example.json sender_config.json
```

First validate the configuration and application without using the network:

```bash
python send_app.py --dry-run
```

After configuring the broker and starting the Pi receiver, send the
application:

```bash
python send_app.py
```

Use a configuration stored elsewhere by passing its path:

```bash
python send_app.py ../my-apps/demo-sender.json
```

`paho-mqtt` uses its current callback API and waits for the QoS 1 publish
acknowledgement as described by the
[Eclipse Paho client documentation](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html).

## Development Mosquitto broker on Raspberry Pi OS

Install Mosquitto on the Pi:

```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Create `/etc/mosquitto/conf.d/iot-app.conf`:

```text
listener 1883 0.0.0.0
allow_anonymous true
```

Then restart it:

```bash
sudo systemctl restart mosquitto
```

Port `1883` now accepts unauthenticated, unencrypted MQTT. Use it only on a
trusted development network. The final product should use TLS, separate
sender/device accounts, a topic ACL, and a signed application package.

Start `iot_app` without MQTT credentials:

```bash
export IOT_DEVICE_ID=raspberrypi-01
export IOT_MQTT_HOST=127.0.0.1
./build/iot_app/iot_app
```

No C++ or Python code change is required for this anonymous development setup.

### Troubleshoot `Connection refused`

This error means that the sender could not connect to the MQTT broker. The
Python application has not reached `iot_app` yet.

```text
iot_app_sender failed: [Errno 111] Connection refused
```

On the Raspberry Pi, confirm its current address and check Mosquitto:

```bash
hostname -I
sudo systemctl status mosquitto --no-pager -l
sudo ss -lntp | grep ':1883'
```

If `ss` shows `127.0.0.1:1883` and `[::1]:1883`, Mosquitto accepts only local
connections. Create `/etc/mosquitto/conf.d/iot-app.conf` with the development
listener shown above, then restart it:

```bash
sudo systemctl restart mosquitto
sudo ss -lntp | grep ':1883'
```

The result should contain `0.0.0.0:1883`. From Ubuntu, test the connection
before running the sender:

```bash
nc -vz rspi-iot-app.local 1883
```

If the mDNS name does not resolve, use the Pi address reported by `hostname -I`
as a temporary fallback. If the service does not restart, read the broker log:

```bash
sudo journalctl -u mosquitto -n 50 --no-pager
```

If Mosquitto listens on `0.0.0.0:1883` but the test still fails, check the
Raspberry Pi firewall. When UFW is active, allow MQTT only from the trusted
local network:

```bash
sudo ufw allow from 192.168.0.0/24 to any port 1883 proto tcp
```

## End-to-end test

Leave `iot_app` running on the Raspberry Pi. On Ubuntu, activate the sender
environment and validate the example without using MQTT:

```bash
cd iot_app_sender
. .venv/bin/activate
python send_app.py --dry-run
```

Then send it:

```bash
python send_app.py
```

The sender should report `received`, `validating`, `starting`, and finally
`started`. The Pi should log the external application's name without restarting
the C++ process. With the default sender configuration, the screen should show
the Ubuntu clock app and its time should change once per second. Its
reconstructed files exist only while needed under:

```text
/tmp/iot-app-<uid>/applications/<transfer-id>/
```

To test a startup failure, select the `traceback_failure` sample. The final
status should be `failed`, and the Pi should show the native red emergency
screen. It shows the application name, failure phase, time, and Python
traceback. The Pi terminal prints the same traceback.

## MQTT message used by this phase

The install topic is specific to a device:

```text
iot/devices/raspberrypi-01/applications/install
```

The JSON payload contains:

```text
message and transfer type
device and transfer IDs
application metadata from app.json
entry-point byte size
entry-point SHA-256
Base64-encoded Python source
```

The entry point is kept only in the application metadata. The source object
does not repeat it:

```json
{
  "application": {
    "id": "ubuntu-clock",
    "name": "Ubuntu clock app",
    "entry_point": "main.py"
  },
  "source": {
    "encoding": "base64",
    "size_bytes": 1234,
    "sha256": "...",
    "content": "..."
  }
}
```

The sender and IoT App calculate the same status topic from the device ID and
transfer ID. It is not repeated in the JSON message:

```text
iot/devices/raspberrypi-01/applications/status/<transfer-id>
```

Expected intermediate statuses are `received`, `validating`, and `starting`.
Final statuses are:

- `started`: the external application entry point finished successfully;
- `rejected`: the message or application did not pass validation;
- `failed`: installation or application startup failed. The native emergency
  screen remains visible when Python startup was attempted.

Base64 is only a JSON representation for binary bytes; it provides no security.
This single-message protocol is limited to small, single-file applications. A
future `.iotapp` archive or chunked-transfer protocol can support multi-file
applications and assets.
