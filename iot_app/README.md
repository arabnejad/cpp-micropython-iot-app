# IoT Application

This directory contains the C++ runtime, embedded MicroPython modules, default
Python application, tests, and Linux-image integration maintained by this
project.

The repository-root `lvgl/`, `micropython/`, `buildroot/`, `poky/`,
`meta-openembedded/`, and `meta-raspberrypi/` directories are pinned upstream
submodules. Project changes should stay outside those directories.

## Language policy

Project C++ targets compile as strict ISO C++17. Prefer C++14-compatible coding
patterns for general application logic. Use C++17 features when they provide
needed functionality or make an implementation clearer.

MicroPython itself is C. Native modules keep hardware and application logic in
C++ and use a thin C-compatible binding layer to expose it to Python.

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
another dependency. The logging macros add `__func__` automatically. The output
sink uses a mutex, so messages written from different threads do not become
mixed together. Both `__func__` and the logger's message-building code are
compatible with C++14.

Terminal output uses the ANSI colours in `iot::logging::Color`:

- `INFO` is green.
- `DEBUG` is cyan.
- `WARNING` is yellow.
- `ERROR` is red.

Colours are added only when the destination is a terminal. Redirected output
and service logs remain plain text. Set `NO_COLOR=1` to disable colours in a
terminal.

The default level is `INFO`. Enable detailed messages for one run with:

```bash
IOT_LOG_LEVEL=DEBUG ./build/iot_app/iot_app
```

`IOT_LOG_LEVEL` also accepts `INFO`, `WARNING` (or `WARN`), `ERROR`, and
`NONE` (or `OFF`). `NONE` disables the project logger. Error messages include
useful context such as the application ID, transfer ID, path, display size, or
render-queue size when that information is available. The final error log in
`main()` is the fallback for an exception that stops the process.

## Source layout

```text
buildroot_external/         Project-owned BR2_EXTERNAL tree
cmake/                      CMake helpers
config/                     Project-owned third-party configuration
default_python_application/ Shipped default Python application
docs/                       Focused runtime, API, hardware, and image guides
tests/                      C++ unit tests and embedded MicroPython tests
include/iot/                Public C++ headers
micropython_config/         Embedded-port configuration and generation wrapper
micropython_iot_modules/    Native IoT modules compiled into MicroPython
src/input/                  Gamepad protocol and input state
src/messaging/              MQTT transport, validation, installation, and deployment control
src/network/                Bounded HTTP and HTTPS file downloads
src/platform/linux/         Linux display, I2C, and system-information support
src/python/                 Embedded interpreter and MicroPython application context
src/ui/                     Process-wide screen manager and LVGL framebuffer backend
src/runtime/                Executable entry point and runtime lifecycle
```

The CMake build uses two internal libraries:

- `iot_platform` contains Linux display discovery, framebuffer rendering,
  JPEG decoding and caching, system information, I2C, and input hardware.
- `iot_runtime` contains MicroPython, native modules, scheduling, MQTT,
  deployment, file downloads, and application supervision.

The `iot_app` executable connects these parts and owns their process lifetime.
The [system design guide](docs/system-design/README.md) explains how the
components work together.

## Build on Raspberry Pi OS

Install the native build dependencies:

```bash
sudo apt update
sudo apt install \
  build-essential cmake pkg-config \
  libdrm-dev libmosquitto-dev libcjson-dev libssl-dev \
  libcurl4-openssl-dev libturbojpeg0-dev ca-certificates \
  mosquitto mosquitto-clients
```

From the repository root:

```bash
make submodules
make iot-app
```

Run IoT App from a Linux console where `/dev/fb0` is available:

```bash
./build/iot_app/iot_app
```

Console mode prevents a desktop compositor from redrawing over the framebuffer.
IoT App uses the active framebuffer size and pixel format; it does not change
the monitor mode. It starts the shipped default Python application and then
listens for replacement applications through MQTT. Stop it with `Ctrl+C`.

The [Raspberry Pi OS guide](docs/raspberry-pi-os/README.md) covers console mode,
display permissions, copying a native build to the Pi, and switching back to
the desktop. The [sender guide](../iot_app_sender/README.md) covers Mosquitto
setup and sending another Python application.

## Runtime settings

The defaults work when IoT App and Mosquitto run on the same Raspberry Pi.
Set an environment variable only when a value must be different:

| Variable | Default | Purpose |
|---|---|---|
| `IOT_DEVICE_ID` | `raspberrypi-01` | Device name used in MQTT topics |
| `IOT_MQTT_HOST` | `127.0.0.1` | MQTT broker host |
| `IOT_MQTT_PORT` | `1883` | MQTT broker port |
| `IOT_MQTT_USERNAME` | Empty | Optional MQTT username |
| `IOT_MQTT_PASSWORD` | Empty | Optional MQTT password |
| `IOT_LOG_LEVEL` | `INFO` | Minimum project log level |

For example:

```bash
# These values are optional. Set them only when the defaults are unsuitable.
export IOT_DEVICE_ID=my-raspberry-pi
export IOT_MQTT_HOST=rspi-iot-app.local

./build/iot_app/iot_app
```

The complete list of fixed limits, including queue, source, download, heap, and
image-cache sizes, is in the
[runtime configuration section](docs/system-design/README.md#20-runtime-configuration).

## Python applications

The local build copies the shipped application beside the executable:

```text
build/iot_app/
├── iot_app
└── default_python_application/
    ├── app.json
    └── main.py
```

`cmake --install`, Buildroot, and Yocto install the same package under
`${CMAKE_INSTALL_DATADIR}/iot-app/default_python_application`, normally
`/usr/share/iot-app/default_python_application`. Applications received through
MQTT are installed temporarily under `/tmp` and are removed when no longer
needed. The shipped default application runs again after the `iot_app` process
restarts.

Application code imports the project-owned modules from `iot`:

```python
from iot import display, input, network, scheduler, system
```

Use the [MicroPython API guide](docs/micropython-api/README.md) for every
function, parameter, return value, limit, and application example. The
[sample application catalog](../iot_app_sender/sample_applications/README.md)
links to runnable applications and lists their hardware requirements.

## Where to find detailed information

Each subject has one main guide. Other documents link to it instead of keeping
a second copy of the same instructions.

| Subject | Main document |
|---|---|
| Component ownership, startup, threads, application lifetime, rendering, deployment, and failure handling | [System design](docs/system-design/README.md) |
| MicroPython functions, parameters, limits, return values, and examples | [MicroPython API](docs/micropython-api/README.md) |
| LVGL concepts, framebuffer backend, widgets, styles, and render-thread behaviour | [LVGL guide](docs/lvgl/README.md) |
| Adafruit gamepad wiring, I2C setup, and false button presses | [Hardware notes](docs/hardware/README.md) |
| MQTT sender setup, broker configuration, topics, statuses, and connection errors | [IoT App Sender](../iot_app_sender/README.md) |
| Shared Wi-Fi, SSH, mDNS, services, device checks, and image troubleshooting | [Device image guide](docs/device-image/README.md) |
| Buildroot preparation, building, flashing, and Buildroot-specific updates | [Buildroot guide](docs/buildroot/README.md) |
| Yocto layers, preparation, building, flashing, and BitBake-specific updates | [Yocto guide](docs/yocto/README.md) |
| Root and `/data` partitions | [Storage guide](docs/storage/README.md) |
| Testing a rebuilt executable from `/data` | [Development executable guide](docs/development-executable/README.md) |
