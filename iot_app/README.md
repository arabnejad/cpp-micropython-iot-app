# IoT Application

This directory holds the code and configuration maintained as part of IoT App,
including the files used by its Buildroot and Yocto images.

The repository-root `lvgl/`, `micropython/`, `buildroot/`, `poky/`,
`meta-openembedded/`, and `meta-raspberrypi/` directories are pinned upstream
submodules and must remain unmodified.

## Language policy

Project C++ targets compile as strict ISO C++17. Prefer C++14-compatible coding
patterns for general application logic. Use C++17 features when they provide
needed functionality or make an implementation clearer.

MicroPython itself is C. Native modules should keep hardware and application
logic in C++ and use only a thin C-compatible MicroPython binding layer.

## Logging

Project C++ code writes runtime messages through `iot::logging::Logger` instead
of writing directly to `std::cout` or `std::cerr`. A class owns a logger named
after that class:

```cpp
logging::Logger m_logger{"ScreenManager"};

IOT_LOG_INFO(m_logger, "Rendering started");
```

That call produces:

```text
[IOT_APP][INFO]      [ScreenManager][start] Rendering started
```

A standalone function uses a logger without a class name:

```cpp
iot::logging::Logger logger;

IOT_LOG_WARNING(logger, "No preferred display was found");
```

If the call is inside `choosePreferredDisplay()`, the output starts with:

```text
[IOT_APP][WARNING]   [choosePreferredDisplay]
```

The `[IOT_APP]` prefix separates project messages from logs written by LVGL or
another dependency. The logging macros add `__func__` automatically. `__func__`
and the logger's message-building code are compatible with C++14. The output
sink uses a mutex, so messages written from different threads cannot be mixed
together.

Terminal output uses the ANSI colours in `iot::logging::Color`:

- `INFO` is green.
- `DEBUG` is cyan.
- `WARNING` is yellow.
- `ERROR` is red.

Colours are added only when the destination is a terminal. Redirected output
and service logs remain plain text. Set `NO_COLOR=1` to disable colours in a
terminal.

The default level is `INFO`, so detailed drawing and deployment parameters do
not fill normal logs. Enable them for one run with:

```bash
IOT_LOG_LEVEL=DEBUG ./build/iot_app/iot_app
```

`IOT_LOG_LEVEL` also accepts `INFO`, `WARNING` (or `WARN`), `ERROR`, and
`NONE` (or `OFF`). `NONE` disables the project logger completely. Error logs
are written where a failure has useful context, such as an application ID,
transfer ID, path, display size, or render-queue size. The final `main()` error
log remains the fallback for exceptions that stop the process.

## Directory layout

```text
buildroot_external/        Project-owned BR2_EXTERNAL tree
cmake/                     CMake helpers
config/                    Project-owned third-party configuration
default_python_application/ Shipped default Python application
docs/                      Focused guides for LVGL, hardware, APIs, system design, and deployment
tests/                     C++ unit tests and embedded MicroPython tests
include/iot/               Public C++ headers
micropython_config/        Embed-port configuration and generation wrapper
micropython_iot_modules/   Native IoT modules compiled into MicroPython
src/input/                 Gamepad protocol and input state
src/messaging/             MQTT receiving, validation, and deployment control
src/platform/linux/        Linux display, I2C, and system-information support
src/python/                Embedded interpreter and MicroPython application context
src/ui/                    Process-wide screen manager and LVGL framebuffer backend
src/runtime/               Executable entry point and runtime lifecycle
```

The CMake build uses two internal libraries:

- `iot_platform` contains Linux display discovery, framebuffer rendering,
  system information, I2C, and input hardware support.
- `iot_runtime` contains MicroPython, native modules, scheduling, MQTT,
  deployment, and application supervision.

The `iot_app` executable is the small composition root that connects those
services and owns the process lifecycle.

## Build on Raspberry Pi OS

On Raspberry Pi OS, install the native build dependencies:

```bash
sudo apt update
sudo apt install \
  build-essential cmake pkg-config \
  libdrm-dev libmosquitto-dev libcjson-dev libssl-dev \
  mosquitto mosquitto-clients
```

From the repository root:

```bash
git submodule update --init --recursive
cmake -S iot_app -B build/iot_app
cmake --build build/iot_app --parallel
```

Run from console mode so the desktop compositor does not redraw over the
framebuffer:

```bash
./build/iot_app/iot_app
```

The runtime automatically uses `HDMI-A-1` when it is connected, otherwise it
uses the first connected display. It always starts the shipped default
application. Replacement applications enter through the validated MQTT
deployment workflow rather than through command-line paths.

The application does not request or change a resolution. LVGL writes to
`/dev/fb0` using the console framebuffer size and pixel format already
configured by Linux. Stop the runtime with Ctrl+C.

The build generates a self-contained MicroPython interpreter and links it and
the native `iot` module into `iot_app`. The target does not require CPython
or a system MicroPython installation. The application source is
kept outside the executable so applications can later be received, saved, and
started without relinking the C++ runtime.

The development build copies the default package beside the executable:

```text
build/iot_app/
├── iot_app
└── default_python_application/
    ├── app.json
    └── main.py
```

`cmake --install`, the Buildroot package, and the Yocto recipe install the same
default package under
`${CMAKE_INSTALL_DATADIR}/iot-app/default_python_application`, normally
`/usr/share/iot-app/default_python_application`. Applications received through
MQTT are reconstructed only under `/tmp`, as described below, and are not
persistent.

Every application directory keeps its metadata in `app.json`:

```json
{
  "id": "default",
  "name": "Default app",
  "entry_point": "main.py"
}
```

`PythonApplicationLoader` validates the application metadata and keeps the
entry point inside its package directory. It applies a source-size limit and
reads the source into an owning `PythonApplication`. This first version executes
one entry-point file. Importing additional Python files from the package will
require later MicroPython filesystem/import integration.

`ScreenManager` opens LVGL once and remains alive for the complete C++ process.
It sends every LVGL operation through one render thread and rejects drawing
when its bounded command queue is full. `PythonApplicationManager` owns the
default/external/emergency state, the current MicroPython interpreter, its
application context, timer updates, and recovery policy. Switching applications
creates a clean interpreter without reopening LVGL.
Python can observe the available display size through `display.size()`, but it
does not control the system resolution.

Monitor discovery still uses libdrm to report connectors, EDID information,
and the active display mode. Rendering does not take DRM master ownership and
does not select a monitor mode. It uses LVGL's Linux framebuffer driver with
`/dev/fb0`, so the console framebuffer and active DRM mode must have the same
width and height.

The packaged default app is an operating-system dashboard. It shows system,
network, display, resource, interface, and device information without assuming
that user-configured hardware is attached. Its information comes from normal
Linux APIs and read-only files under `/proc`, `/sys`, and `/dev`.

Python applications can read the same snapshot:

```python
from iot import display, system

system_information = system.information()
resource_information = system.resources()
network_interfaces = system.network_interfaces()
system_interface_counts = system.interfaces()
connected_device_counts = system.devices()
application_information = system.app_information()
connected_monitors = display.monitors()
active_monitor = display.active_monitor()
```

The complete [MicroPython API guide](docs/micropython-api/README.md) lists every
project-owned native module, its required and optional parameters, return
values, lifecycle rules, and a working example for each module.

The [system design document](docs/system-design/README.md) explains how the
runtime, rendering, MicroPython, MQTT deployment, hardware support, recovery,
Buildroot, and Yocto integration work together.

The [LVGL guide](docs/lvgl/README.md) introduces LVGL and explains
the project's framebuffer backend, render thread, widgets, styles,
configuration, drawing functions, and extension process.

`system.interfaces()` counts operating-system interfaces such as `/dev/i2c-*`
and `/dev/gpiochip*`; it does not probe unknown I2C addresses. Device counts
cover items Linux can safely enumerate, such as USB, input, and block devices.
The snapshot is captured when an application starts, so the initial dashboard
is consistent and does not require a Python polling loop.

## Scheduled Python updates

An application's `main.py` sets up its screen and then returns. Register a short
scheduler callback for anything that needs to change later:

```python
from iot import display, scheduler, system

clock_box = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text=system.current_time(),
)

def update_clock():
    display.update_text_box(clock_box, system.current_time())

def show_next_image():
    # Change the existing slideshow object here.
    pass

clock_timer = scheduler.every(milliseconds=1000, callback=update_clock)
slideshow_timer = scheduler.every(milliseconds=10000, callback=show_next_image)
```

Each timer keeps its own interval. In this example, the clock runs every second
and the slideshow runs every ten seconds. Python does not need an infinite
loop, a background thread, or a one-second C++ polling loop. The C++ runtime
waits until the nearest timer is due; an MQTT message can wake that wait early
when a new application arrives.

`scheduler.every()` returns a positive timer ID. Stop one timer with
`scheduler.cancel(timer_id)`, or remove every timer owned by the current app
with `scheduler.clear()`. Timer callbacks run one at a time on MicroPython's
owner thread, so they should finish quickly. Display calls from a callback are
queued for the existing LVGL render thread. A callback can schedule or cancel
timers safely, but it should **not** contain a **permanent loop**.

`display.draw_text_box()` returns the ID of the newly created text box.
`display.update_text_box(widget_id, text)` changes that same box, avoiding a
new LVGL object every second. `display.move_text_box(widget_id, x, y)` moves
the existing box to an absolute screen position without recreating it.
`display.delete_text_box(widget_id)` removes one text box when it is no longer
needed; `display.clear()` still removes everything from the application screen.
Text defaults to white. Its black background is transparent and borderless by
default. Set `background_opacity=255` and `border_width=2` when a visible panel
is needed. Background opacity accepts values from 0 (transparent) to 255
(solid). These defaults live in the C++ `TextBoxSpec`; the MicroPython binding
only forwards style values supplied by the application.
`system.current_time()` returns the Raspberry Pi's local date and time as
`YYYY-MM-DD HH:MM:SS`.
`system.uptime_seconds()` performs a small live uptime read for the same header
update. `system.network_interfaces()` also performs a live read, and the
default dashboard refreshes its Network panel every five seconds. CPU
temperature, load, memory, storage, interface, and device panels remain startup
snapshots.

If a scheduled callback raises an exception, the interpreter is stopped. The
C++ runtime shows a red emergency screen with the failed application's name,
failure phase, time, and Python traceback. The shipped default app is not
started again. It runs again only after `iot_app` restarts. The traceback is
also written to the terminal or service log.

The runtime keeps at most 8 KiB from the end of a traceback, where the final
exception message normally appears. It writes the traceback to the log and
shows it on the native emergency screen.

## Receive an application from Ubuntu through MQTT

`iot_app` always starts the shipped default Python dashboard first. It also
connects to an MQTT broker and subscribes to:

```text
iot/devices/<device-id>/applications/install
```

The connection uses these environment variables:

```text
IOT_DEVICE_ID       default: raspberrypi-01
IOT_MQTT_HOST       default: 127.0.0.1
IOT_MQTT_PORT       default: 1883
IOT_MQTT_USERNAME   default: empty
IOT_MQTT_PASSWORD   default: empty
```

For the first test on a trusted private network, create
`/etc/mosquitto/conf.d/iot-app.conf` on the Raspberry Pi with anonymous access:

```text
listener 1883 0.0.0.0
allow_anonymous true
```

Then start the broker and runtime:

```bash
sudo systemctl enable --now mosquitto
sudo systemctl restart mosquitto

# These exports are optional. Uncomment and change them only when your device
# ID or broker address is different from the defaults listed above.
# export IOT_DEVICE_ID=my-raspberry-pi
# export IOT_MQTT_HOST=rspi-iot-app.local

./build/iot_app/iot_app
```

### Troubleshoot `Connection refused`

If the Ubuntu sender reports `[Errno 111] Connection refused`, the message has
not reached `iot_app`. First confirm that the Raspberry Pi still has the IP
address used by the sender:

```bash
hostname -I
```

Then check the broker and its listening address:

```bash
sudo systemctl status mosquitto --no-pager -l
sudo ss -lntp | grep ':1883'
```

A running Mosquitto service is not enough. Raspberry Pi OS may start it with
only these local listeners:

```text
127.0.0.1:1883
[::1]:1883
```

Those addresses accept MQTT connections from the Pi but reject the Ubuntu
sender. Check that `/etc/mosquitto/mosquitto.conf` loads the configuration
directory:

```bash
grep -n 'include_dir' /etc/mosquitto/mosquitto.conf
```

It should include `/etc/mosquitto/conf.d`. After creating the
`iot-app.conf` file shown above, restart Mosquitto and check again:

```bash
sudo systemctl restart mosquitto
sudo ss -lntp | grep ':1883'
```

The listener should now be `0.0.0.0:1883`, which accepts connections through
the Pi's network interfaces. Test it from Ubuntu before running the sender:

```bash
nc -vz rspi-iot-app.local 1883
```

If the mDNS name does not resolve, use the current address reported by
`hostname -I` as a temporary fallback. If Mosquitto fails to restart, read its
error log:

```bash
sudo journalctl -u mosquitto -n 50 --no-pager
```

No code change is needed for anonymous MQTT. Empty username and password
settings tell the runtime not to send credentials. Anonymous port 1883 is
unencrypted and must be used only for development on a trusted network. Add
TLS, separate device/sender accounts, and topic ACLs before production.

The MQTT callback never calls MicroPython or LVGL. It copies the message into a
four-entry queue and returns. The main thread checks the JSON, target device,
source size, Base64 data, and SHA-256 before changing the running application.

Received applications are temporary:

```text
/tmp/iot-app-<uid>/applications/<transfer-id>/
├── app.json
└── <entry-point>.py
```

The runtime clears its private temporary application directory when it starts,
and the operating system clears `/tmp` on reboot. No external application is
copied to `/usr` or `/var`; after reboot the shipped default app runs again.

If validation fails, the current app remains untouched. If a valid external
app raises an exception while its `main.py` starts or later from a scheduled
callback, `iot_app` destroys that MicroPython session and shows the captured
traceback on its native emergency screen. The C++ process and MQTT connection
remain alive, but no Python application is running. The emergency screen stays
visible until another valid external app is sent or `iot_app` restarts. A
restart runs the shipped default app again.
MQTT QoS 1 duplicates are recognized by transfer ID and receive the
already-recorded final result instead of being executed twice.

This first receiver supports one Python entry point per message. The entry point
must perform its startup work, register any recurring work with
`scheduler.every()`, and return. A same-process interpreter cannot safely
force-stop Python code stuck in an infinite loop. Multi-file imports and
archive delivery remain later features.


## I2C gamepad API

The input layer is synchronous. The OS-only default dashboard does not probe
for a gamepad. A hardware-specific application can create and read one
explicitly from C++:

```cpp
iot::input::AdafruitMiniI2cGamepad gamepad{1, 0x50};
gamepad.connect();
gamepad.calibrateJoystick();
gamepad.refreshInputState();

const auto direction = gamepad.joystick().direction();
const bool aPressed =
    gamepad.buttons().isPressed(iot::input::GamepadButton::A);
```

Python applications that care only about direction can use the calculated
name without reading X/Y coordinates:

```python
gamepad.refresh_input_state()
direction = gamepad.joystick().direction()

if direction == "left":
    print("Joystick moved left")
```

Possible names are `center`, `left`, `right`, `up`, `down`, `up_left`,
`up_right`, `down_left`, and `down_right`. Calibration values and X/Y readings
remain internal inputs to this calculation; an application does not need to
use them.

Application code can accept the hardware-independent `GameController`
interface. The Adafruit driver keeps its product 5743 pin mapping and Seesaw
register protocol private, so a controller from another manufacturer can
implement the same interface with a different transport or protocol.

`I2cDevice` uses the Linux `/dev/i2c-N` interface directly; no system Python or
libi2c runtime library is required. The user needs permission to open the I2C
device, normally through the `i2c` group on Raspberry Pi OS.

### Troubleshoot a missing `/dev/i2c-1`

If a gamepad application fails during startup with this error, Raspberry Pi OS
has not enabled its ARM I2C interface:

```text
RuntimeError: Could not open /dev/i2c-1: No such file or directory
```

Enable I2C and reboot:

```bash
sudo raspi-config nonint do_i2c 0
sudo reboot
```

In this `raspi-config` command, `0` means enable. After the reboot, confirm that
the device exists:

```bash
ls -l /dev/i2c-1
```

Install the I2C command-line tools if needed, then check bus 1:

```bash
sudo apt install i2c-tools
i2cdetect -y 1
```

The Adafruit gamepad normally appears as `50` at address `0x50`. If the bus
exists but `50` is missing, check the gamepad's power, SDA, SCL, and ground
connections.

The same setting can be enabled through `sudo raspi-config` by selecting
`Interface Options`, `I2C`, and `Enable`. Raspberry Pi's
[configuration guide](https://www.raspberrypi.com/documentation/configuration/raspberry-pi.html)
describes both methods.

The physical input masks, active-low behavior, and A-button example are
explained in [the hardware notes](docs/hardware/README.md). The same guide also
explains how to troubleshoot false button presses and configure the 400 kHz
I2C speed recommended for this Adafruit gamepad.

## Configure Buildroot

For Buildroot preparation, image creation, flashing, and incremental rebuilds,
see the [Buildroot image guide](docs/buildroot/README.md). Wi-Fi, SSH, service
commands, and troubleshooting are in the
[shared device-image guide](docs/device-image/README.md).

```bash
make buildroot-prepare
```

This command selects the Raspberry Pi 4 Model B development image. The root
Makefile keeps the output in
`/opt/iot-app-builds/buildroot-raspberry-pi-4`. This directory survives a
reboot and avoids the `@` character found in some home-directory paths.

The external defconfig enables `iot_app`, `libdrm`, Mosquitto, cJSON, OpenSSL,
Raspberry Pi VC4 DRM, and DRM framebuffer emulation for `/dev/fb0`. LVGL and
MicroPython are compiled from their pinned root submodules as part of the CMake
build. Neither submodule is modified.

The package creates a non-login `iot-app` service account with access through
the `video`, `render`, `i2c`, and `input` groups. The default BusyBox image uses
the board overlay's `mdev.conf` to assign matching device-node permissions.
The BusyBox/SysV image installs the service as `/etc/init.d/S90iot-app`, so it
starts automatically near the end of boot. `/etc/init.d/iot-app` is an alias
for manual start, stop, and restart commands. A systemd build installs
`iot-app.service` and the matching udev rules.

The Buildroot development image exposes an anonymous, unencrypted Mosquitto
listener on IPv4 port 1883 so the Ubuntu sender can deploy applications over a
trusted local network. This is only a development setup. A production image
must replace it with TLS, separate device and sender credentials, topic ACLs,
and signed application packages.

## Configure Yocto

The repository also contains the project-owned `meta-iot-app` layer. It builds
the same CMake application and default Python package into a systemd-based
Raspberry Pi 4 image.

Prepare and check the Yocto configuration with:

```bash
make yocto-prepare
make yocto-check
```

Buildroot and Yocto use the same private `wpa_supplicant.conf` file in the
repository root. Run `make wifi-prepare`, then add the network name and
password to that file before building either image. Git ignores the private
file.

See the [Yocto image guide](docs/yocto/README.md) for host dependencies,
building, Raspberry Pi Imager, and incremental updates. Shared Wi-Fi, SSH,
service, and troubleshooting instructions are in the
[device-image guide](docs/device-image/README.md).
