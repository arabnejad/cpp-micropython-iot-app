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
message. The sender then waits for IoT App to reply `accepted`, which means
the device checked the package and installed its temporary files. Each send
has its own transfer ID and status topic.

`accepted` does not mean the Python code compiled or ran successfully. The
sender exits after acceptance; compilation and execution happen on the device.

The C++ receiver starts the shipped default app before subscribing. A valid
external app replaces the current Python session without restarting the C++
process. If compilation, startup, or a scheduled callback fails, the device
writes the traceback to its log and shows its native emergency screen. It does
not send another deployment status. The default app runs again only
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

The sender uses these defaults from `send_app.py`:

```text
MQTT keep alive:             60 seconds
MQTT connection timeout:     10 seconds
Device reply wait:           30 seconds
Maximum deployment message:  1,000,000 bytes
```

The fixed message limit remains below IoT App's 1,048,576-byte MQTT limit. The
decoded Python entry point also has a separate 524,288-byte limit on the
Raspberry Pi.

The sender waits up to 30 seconds for acceptance or a validation/installation
error. It does not wait for `main.py`, downloads, or scheduled callbacks. Each
HTTP download on the device has its own 30-second transfer limit, including
up to 10 seconds to connect.

If the device reply does not arrive in time, the sender reports a timeout. This
does not cancel the request or prove it was rejected: the device may be busy
running the previous app, or its reply may not have arrived. Check the device
log before sending again. Use `--no-wait` to exit after the broker
acknowledgement without confirming that the device accepted the application.

Update both IoT App and the sender together. Older versions use startup-result
statuses and do not follow this acceptance-only exchange.

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

The sender should report `received`, `validating`, and finally `accepted`.
The Pi should log the external application's name without restarting
the C++ process. With the default sender configuration, the screen should show
the Ubuntu clock app and its time should change once per second. Its
reconstructed files exist only while needed under:

```text
/tmp/iot-app-<uid>/applications/<transfer-id>/
```

To test a startup failure, select the `traceback_failure` sample. The sender
still reports `accepted` and exits successfully because the package was
installed. The Pi then shows the native red emergency screen with the
application name, failure phase, time, and Python traceback. The Pi log prints
the same traceback. A syntax error is handled in the same way.

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

Expected intermediate statuses are `received` and `validating`.
Final statuses are:

- `accepted`: the package passed validation and its temporary files are
  installed. This reply is sent before compiling or executing Python;
- `rejected`: the message or application metadata did not pass validation;
- `failed`: the temporary application could not be installed.

For `rejected` or `failed`, the current app or emergency screen stays unchanged
and the sender exits with code `2`. For `accepted`, it exits with code `0`.
Python errors after acceptance appear on the device, not as another sender
result. Connection errors and reply timeouts use exit code `1`.

Base64 is only a JSON representation for binary bytes; it provides no security.
This single-message protocol is limited to small, single-file applications. A
future `.iotapp` archive or chunked-transfer protocol can support multi-file
applications and assets.
