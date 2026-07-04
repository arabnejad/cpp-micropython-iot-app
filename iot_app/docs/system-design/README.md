# IoT App system design

## 1. Purpose

IoT App is a C++ program for an embedded Linux device such as a Raspberry Pi.
It owns the display, embeds MicroPython, and gives Python applications a small
set of native APIs for drawing, reading system information, scheduling work,
and using supported hardware.

The device always starts with a default Python application shipped with the
C++ executable. A development computer can send another application through
MQTT. IoT App stops the current Python interpreter, creates a clean one, and
starts the received application without restarting the C++ process.

If a Python application fails, IoT App stops its interpreter and shows the
traceback on the native emergency screen. The shipped default app runs again
only after the C++ process restarts.

This document follows the full runtime from startup to drawing and application
deployment. The shorter guides are better for day-to-day tasks:

- [IoT App README](../../README.md) for building, running, and configuring the
  runtime.
- [MicroPython API guide](../micropython-api/README.md) for Python functions,
  parameters, return values, and examples.
- [Sender guide](../../../iot_app_sender/README.md) for deploying an
  application over MQTT.
- [Buildroot guide](../buildroot/README.md) for building and flashing an image.
- [Yocto guide](../yocto/README.md) for building the equivalent image with
  Yocto and the project-owned layer.

## 2. What the system is responsible for

IoT App handles these jobs:

- Find the connected display and read its active mode.
- Draw directly to the Linux framebuffer with LVGL.
- Run one MicroPython application at a time.
- Expose project-owned native modules as `iot.display`, `iot.input`,
  `iot.scheduler`, and `iot.system`.
- Start the shipped default application when the process starts.
- Receive a single-file Python application through MQTT.
- Validate, install, start, and report the result of a deployment.
- Recover from Python startup and scheduled-callback failures.
- Build on Raspberry Pi OS or as part of a Buildroot or Yocto image.

The system does not currently:

- Change the monitor resolution.
- Run more than one Python application at a time.
- Load extra Python files from an application package.
- Provide a security sandbox for untrusted Python code.
- Verify a package signature or the identity of its author.

## 3. System at a glance

The runtime is one Linux process with three important threads. An application
moves through the system in this order:

```text
Ubuntu computer
└── iot_app_sender
        │
        │ 1. Publishes the Python application
        v
    MQTT broker
        │
        │ 2. Forwards the application
        v
┌────────────────────────── Raspberry Pi ───────────────────────────┐
│                                                                   │
│  MQTT thread                                                      │
│  Receive the MQTT message                                         │
│  (MqttApplicationReceiver)                                        │
│       │                                                           │
│       v                                                           │
│  Pass the message safely to the main thread                       │
│  (ApplicationMessageQueue)                                        │
│       │                                                           │
│       v                                                           │
│  Main thread                                                      │
│  Validate and install the application                             │
│  (ApplicationDeploymentController)                                │
│       │                                                           │
│       v                                                           │
│  Stop the current app, run the new app, and handle its timers     │
│  (PythonApplicationManager)                                       │
│       │                                                           │
│       │ Python drawing requests                                   │
│       v                                                           │
│  Place drawing work in the ScreenManager command queue            │
│       │                                                           │
│       v                                                           │
│  Render thread                                                    │
│  LVGL -> /dev/fb0 -> HDMI monitor                                 │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

The deployment result travels back separately:

```text
Raspberry Pi -- success or failure --> MQTT broker --> Ubuntu sender
```

The main thread controls the process. It starts and stops Python, validates
deployments, and runs scheduled callbacks. The MQTT thread never starts an
application, and the render thread never runs Python. Each thread therefore
uses only the library and state it owns.

### 3.1 Application deployment pipeline

The deployment code is split into small classes, but they form one pipeline:

```text
MqttApplicationReceiver
    |  receives the MQTT message
    v
ApplicationMessageQueue
    |  passes the message to the main thread
    v
ApplicationDeploymentController
    |
    +--> ApplicationDeploymentMessageParser
    |      validates the message and reads the application
    |
    +--> TemporaryPythonApplicationInstaller
    |      writes the application under /tmp
    |
    +--> PythonApplicationManager
    |      stops the current app and starts the received app
    |
    +--> MqttApplicationReceiver
           publishes progress and the final result
```

`ApplicationDeploymentController` coordinates the work. The other classes
continue to handle MQTT, thread communication, validation, temporary files,
and Python execution separately. Keeping those jobs separate makes each part
easier to follow and test without creating one large MQTT manager.

## 4. Main design choices

### 4.1 C++ owns the device; Python owns application behavior

C++ handles resources that need careful lifetime management: file descriptors,
threads, LVGL, MicroPython, MQTT, display discovery, and application failures.
Python decides what appears on the screen and how often application-level work
runs.

Python applications can stay small, and the device does not need a separate
Python installation.

### 4.2 One C++ process, one Python interpreter at a time

The C++ process stays alive while applications change. Each application gets a
new MicroPython interpreter and a new fixed-size heap. Stopping an application
destroys its interpreter, scheduled callbacks, Python objects, and hardware
objects owned by those Python objects.

The display service stays alive across these switches. Reopening the
framebuffer for every Python application would add delay and make failure
handling more fragile.

### 4.3 LVGL runs on one dedicated thread

LVGL is **not** treated as thread-safe. Only the render thread calls it. Other
parts of the process send small commands to `ScreenManager`.

The command queue has a fixed capacity. A Python application that draws faster
than LVGL can process the work gets an error instead of consuming memory
without a limit.

### 4.4 MicroPython stays on the main thread

The interpreter records the thread that created it. Startup code and scheduled
callbacks must run on that same thread. MQTT callbacks therefore place messages
in a queue rather than switching applications directly.

### 4.5 Received applications live only in `/tmp`

Received source is reconstructed under a private directory in `/tmp`. It is
removed on replacement and naturally disappears after a reboot. The shipped
default application stays in the installed application data directory.

### 4.6 Logging

Classes that produce runtime messages own an `iot::logging::Logger` with their
class name. The logging macros add the current function name through C++'s
`__func__` value. Standalone functions use a logger without a class name. This
gives class messages the form
`[IOT_APP][LEVEL]     [ClassName][functionName]` and standalone messages the
form `[IOT_APP][LEVEL]     [functionName]`. The project prefix distinguishes
these messages from dependency output, and the level column has a fixed width
so the class and function names line up in terminal output.

The logger supports debug, information, warning, and error messages. One mutex
protects the output sink, so messages from different threads are written as
complete lines. The logger catches its own output errors because a failed log
message must not hide or replace the original application error.

Interactive terminal output uses green for information, cyan for debug, yellow
for warnings, and red for errors. ANSI colour codes are not written when output
is redirected or captured by a service. `NO_COLOR=1` also disables colour.

The minimum level defaults to `INFO`. Setting `IOT_LOG_LEVEL=DEBUG` enables
detailed UI command parameters, MQTT message sizes, application paths, and
deployment state changes. `WARNING` and `ERROR` can be used for quieter product
logs. Errors are logged where an application or subsystem failure is handled
and useful context is available. They are not repeated before every low-level throw,
because that would produce several copies of the same failure.

## 5. Build-time structure

The CMake build creates two internal libraries and one executable:

```text
iot_platform
  shared thread-safe logging
  Linux display discovery
  Linux framebuffer rendering
  system information
  I2C transport
  game controller support

iot_runtime
  embedded MicroPython
  native Python modules
  application loading and supervision
  scheduler
  MQTT receiving and deployment

iot_app
  runtime configuration
  main() and process lifecycle
```

`iot_app` links both libraries. There is no library for every directory. The
two-library split is enough to separate Linux and hardware support from
application-runtime behavior.

### 5.1 Third-party components

| Component | Use in this project |
|---|---|
| MicroPython | Runs the shipped or received Python application inside the C++ process. It is compiled into `iot_app`, so the target system does not need a separate Python installation. Each application starts with a new interpreter. |
| LVGL | Creates the on-screen widgets and draws them through the Linux framebuffer. Only the render thread calls LVGL because the project does not treat it as thread-safe. |
| libdrm | Finds connected displays and reads their connector, EDID, and active-mode information. It does not render the UI or change the display resolution. |
| libmosquitto | Connects to the MQTT 5 broker, receives application-install messages, and publishes deployment results. Its network callbacks place received work in a queue for the main thread. |
| cJSON | Reads application metadata and incoming deployment JSON, and creates the JSON used for deployment status replies. |
| OpenSSL Crypto | Decodes the Base64 Python source carried in JSON and verifies its SHA-256 hash. Base64 is only an encoding, and SHA-256 only detects inconsistent or damaged content; neither one proves who sent the application. |

MicroPython and LVGL are pinned repository submodules. Project code does not
modify those source trees. CMake generates the MicroPython embed sources into
the build directory and compiles them into `iot_runtime`. LVGL is also compiled
into the application. The target device does not need CPython or a separately
installed MicroPython runtime.

## 6. Runtime ownership

`main()` is the composition root. It creates the long-lived objects and keeps
their lifetimes visible in one place.

```text
main()
├── DisplayManager
├── LinuxSystemInformationProvider
├── PythonApplicationLoader
├── ScreenManager
│   └── IRenderBackend (LVGL framebuffer implementation)
├── PythonApplicationManager
│   ├── MicroPythonApplicationContext    created per Python app
│   └── MicroPythonRuntime               created per Python app
├── ApplicationMessageQueue
├── MqttApplicationReceiver
├── TemporaryPythonApplicationInstaller
└── ApplicationDeploymentController
```

Most service objects are not copyable or movable. They own a thread, an open
device, an interpreter, or references to another long-lived service. Keeping
one owner avoids stale callbacks and double cleanup.

### 6.1 Lifetime groups

Process-long objects:

- Display discovery and system-information providers
- `ScreenManager` and the LVGL framebuffer backend
- MQTT receiver and message queue
- Application loader, installer, deployment controller, and application
  manager

Application-long objects:

- `MicroPythonApplicationContext`
- `MicroPythonRuntime`
- The MicroPython heap
- Python globals, callbacks, widgets IDs, and Python-created hardware objects

The screen itself is process-long, but its application widgets are cleared
when the active application changes.

## 7. Threading model

### 7.1 Main thread

The main thread handles:

- Loads configuration and the default application.
- Discovers and selects the active display.
- Creates and destroys MicroPython interpreters.
- Executes the active application's Python entry point.
- Runs scheduled Python callbacks.
- Parses and validates deployment messages.
- Writes received applications into `/tmp`.
- Changes the application state.
- Handles `SIGINT` and `SIGTERM` through a stop flag.

MicroPython never moves away from this thread.

### 7.2 Render thread

`ScreenManager::start()` creates the render thread and waits until the backend
has opened `/dev/fb0`. The thread then repeats this work:

1. Take queued rendering commands in order.
2. Run each command through the backend.
3. Call `lv_timer_handler()` so LVGL can update the display.
4. Wait for LVGL's requested delay, a new command, or shutdown.

The wait is kept between 1 and 50 milliseconds. A new command wakes the thread
immediately.

If backend initialization fails, `start()` receives the exception. If the
thread fails later, the exception is stored. The main loop calls
`throwIfRenderThreadFailed()` regularly and stops the process instead of
continuing with a dead display.

### 7.3 MQTT network thread

Libmosquitto owns the MQTT network thread. Its message callback performs only
small, bounded work:

1. Confirm the topic and payload are present.
2. Reject a message over the configured size limit.
3. Copy the payload into `ApplicationMessageQueue`.
4. Return to libmosquitto.

It does not parse JSON, write files, run Python, or change the screen. Those
operations stay on the main thread.

### 7.4 Communication between threads

| Sender | Receiver | What happens |
|---|---|---|
| MQTT network thread | Main thread | The MQTT callback copies the received JSON text into `ApplicationMessageQueue`. The main thread wakes up, removes the message with `waitAndPopMessage()`, and processes it. If the queue is full, the new message is rejected. |
| Main thread | Render thread | A Python drawing request becomes a C++ drawing command. `ScreenManager::enqueueRenderCommand()` adds it to the render queue and wakes the render thread. The render thread removes the command and uses LVGL to draw it. If the queue is full, the drawing request reports an error. |
| Render thread, during startup | Main thread | The render thread reports whether the display backend opened successfully. It uses a promise to send the result and the main thread waits for it through a future. |
| Render thread, after startup | Main thread | If rendering fails, the render thread saves the exception. The main loop finds it through `ScreenManager::throwIfRenderThreadFailed()` and stops the process safely. |

The two queues have fixed size limits. This prevents incoming applications or
drawing requests from using memory without a limit.

Each queue has a condition variable that wakes its receiving thread. The queue
holds the message or drawing command; the condition variable only provides the
wake-up signal.

No thread calls MicroPython from a callback owned by another thread.

## 8. Startup sequence

Normal startup follows this order:

```text
1. Read command-line and environment configuration
2. Scan DRM devices for connected displays
3. Prefer HDMI-A-1, otherwise choose the first connected display
4. Read that display again and capture its active DRM mode
5. Find and load the shipped default application
6. Start ScreenManager and open /dev/fb0 on the render thread
7. Create the Python application manager
8. Create a fresh MicroPython interpreter
9. Run the default application's main.py
10. Start the MQTT receiver
11. Enter the main event loop
```

The runtime stops startup if it cannot find a connected display, cannot match
the framebuffer size to the active mode, cannot load the default package, or
cannot start the display backend.

MQTT is different: a failure to start MQTT is logged, but the default dashboard
continues to run. Local display operation does not depend on the broker being
available.

## 9. Main event loop

The main loop connects application scheduling and deployment receiving:

```text
check render thread
       |
       v
ask Python scheduler for its next deadline
       |
       v
wait for MQTT message until that deadline
       |
       +---- message arrived ----> process one deployment
       |
       v
run Python callbacks that are now due
       |
       +----> repeat
```

The wait is never longer than one second. The message queue wakes it early when
MQTT receives a deployment. If several messages are already queued, the next
wait returns immediately.

Using the next timer deadline means the runtime does not wake every second just
to ask Python whether work exists. A clock may run every second while another
callback runs every ten seconds; each timer keeps its own interval.

## 10. Display discovery subsystem

Display discovery and rendering are separate.

`DisplayManager` reads information through DRM/KMS:

1. List primary DRM devices named `/dev/dri/cardN`.
2. Open each card and read its DRM resources.
3. Keep connected connectors that report at least one mode.
4. Build connector names such as `HDMI-A-1`.
5. Follow connector to encoder to CRTC to find the mode active now.
6. Read EDID when available.
7. Return monitor details and supported modes as normal C++ values.

`DisplayManager` owns the libdrm-specific work. The rest of IoT App receives
normal C++ monitor and mode values and does not work with libdrm objects.

Linux uses the word "card" for a graphics device. On a Raspberry Pi this is
normally built-in VC4 display hardware, not a removable graphics card.

The EDID parser extracts:

- Manufacturer code
- Monitor model name
- Text or numeric serial number

Bad or missing EDID does not stop rendering. Those descriptive fields remain
empty while DRM connector and mode information is still used.

### 10.1 Display selection

The current policy is intentionally simple:

- Use `HDMI-A-1` when it is connected.
- Otherwise use the first connected display from the sorted DRM scan.
- Stop startup when no connected display exists.

The selected monitor is read again before use so a cable change between the
first scan and selection is detected.

`main()` moves the monitor list from this startup scan into
`PythonApplicationManager`. The manager keeps that one snapshot and each
MicroPython application context reads it without copying the list. Starting or
switching a Python application does not scan DRM again.

After importing it with `from iot import display`, Python can read the snapshot
with `display.monitors()` and identify the selected monitor with
`display.active_monitor()`. The current runtime does not support display
hot-plug. Restarting IoT App performs a new scan.

## 11. Rendering subsystem

The renderer uses LVGL's Linux framebuffer backend with `/dev/fb0`.

Linux chooses the display resolution before IoT App starts. IoT App does not
request a mode and does not restore one on exit. At initialization it checks
that the framebuffer width and height match the active DRM mode. A mismatch is
reported as a startup error because drawing with two different sizes gives
unreliable output.

The framebuffer path keeps the target small. It does not require a desktop,
window manager, Mesa, EGL, or OpenGL.

### 11.1 `ScreenManager`

`ScreenManager` is the thread-safe entry point used by the rest of the process.
It accepts simple C++ values such as `TextBoxSpec`, copies them into commands,
and returns quickly.

Its responsibilities are:

- Own the render thread and backend.
- Give each text box a process-wide widget ID.
- Keep render commands in order.
- Bound the number of pending commands.
- Drop old pending commands when a new application clears the screen.
- Report render-thread failure to the main thread.

Dropping pending commands during `clear()` matters during an application
switch. It prevents delayed drawing from the old app appearing on the new app's
screen.

### 11.2 LVGL framebuffer backend

During startup, the backend creates an LVGL display and opens `/dev/fb0` through
LVGL's Linux framebuffer support. Other components send drawing requests
through `ScreenManager` and do not call the framebuffer functions directly.

The current backend supports:

- Clear screen
- Create, update, move, and delete text boxes
- Draw solid areas
- Show a runtime-owned error screen

The backend stores the outer LVGL object for moving and deleting a text box and
its inner label for changing text. Deleting the outer object also deletes the
label.

The error screen is placed on LVGL's top layer. Python code cannot cover it
with a normal application widget. When an application fails, its interpreter
is stopped and this native screen remains visible.

Font size requests map to the Montserrat fonts compiled into LVGL: 14, 20, 24,
or 32 pixels. A Python app cannot select a font family yet.

### 11.3 How a Python text box reaches the monitor

When Python asks to draw a text box, the request passes through these parts of
the system:

```text
Python application
  display.draw_text_box(...)
          |
          v
MicroPython display module
  display_draw_text_box()
          |
          v
C-to-C++ display bridge
  iot_display_draw_text_box()
          |
          v
ScreenManager
  drawTextBox() -> enqueueRenderCommand()
          |
          v
Render thread
  runRenderLoop()
          |
          v
LVGL framebuffer backend
  createTextBox() -> createTextBoxWidgets()
          |
          v
LVGL -> /dev/fb0 -> HDMI monitor
```

The complete flow is:

1. The Python application calls `display.draw_text_box(...)` after importing
   `display` from `iot`.
2. `display_draw_text_box()` in `mod_iot_display.c` reads the Python arguments.
   It checks the required values and reads any optional colours, opacity,
   border width, and font size.
3. The C module calls `iot_display_draw_text_box()` in
   `display_cpp_bridge.cpp`.
4. The bridge creates a C++ `TextBoxSpec`. It gets the current
   `ScreenManager` from `MicroPythonApplicationContext` and calls
   `ScreenManager::drawTextBox()`.
5. `drawTextBox()` assigns a widget ID to the new text box. It creates a small
   command that will later call `IRenderBackend::createTextBox()`.
6. `ScreenManager::enqueueRenderCommand()` puts that command in the bounded
   render queue and wakes the render thread.
7. `ScreenManager::runRenderLoop()` removes the command from the queue and runs
   it on the render thread.
8. `LvglFramebufferRenderBackend::createTextBox()` creates the LVGL box and
   label. Its helper, `createTextBoxWidgets()`, applies the requested size,
   position, colours, border, opacity, text, and font size.
9. The render loop calls
   `processEventsAndGetWaitMilliseconds()`. This calls LVGL's
   `lv_timer_handler()`, allowing LVGL to update `/dev/fb0`. Linux then sends
   the framebuffer image to the active HDMI monitor.

`draw_text_box()` returns the widget ID after the command has been queued. The
Python app keeps this ID and passes it to `update_text_box()`,
`move_text_box()`, or `delete_text_box()` when it wants to change the same text
box later.

## 12. Python application package

Every application directory contains:

```text
application-directory/
├── app.json
└── main.py                 or another relative .py entry point
```

The `app.json` metadata has three required strings:

```json
{
  "id": "clock",
  "name": "Clock",
  "entry_point": "main.py"
}
```

The application ID may contain letters, numbers, `.`, `-`, and `_`, up to 128
characters. The entry point must be a relative `.py` path that stays inside the
package.

### 12.1 Application loader

PythonApplicationLoader reads an application that already exists on disk.
IoT App uses it for the shipped default application:

1. Resolve the application directory to a canonical path.
2. Read `app.json` with a 64 KiB limit.
3. Parse and validate all required metadata fields.
4. Resolve the entry point and prove it stays inside the package.
5. Check that the entry point is a regular file.
6. Read source with the configured size limit.
7. Reject empty source and null bytes.
8. Return a `PythonApplication` containing its own source-code copy.

The manager does not depend on the source file staying open. It compiles the
owned source string from memory.

Applications received through MQTT have already passed the deployment message
parser before they reach the installer. TemporaryPythonApplicationInstaller
writes that checked metadata and source into a staging directory, renames it
to its final directory, and returns a PythonApplication using the same
checked values. It does not reopen and parse the files that it just wrote.

### 12.2 Default application location

During development, CMake copies the default package next to the executable:

```text
build/iot_app/
├── iot_app
└── default_python_application/
    ├── app.json
    └── main.py
```

An installed build normally uses
`/usr/share/iot-app/default_python_application`. Startup first checks beside
the executable, then the matching install prefix, and then the compiled
installation path.

### 12.3 Shipped dashboard

The current default application is a Python dashboard. At startup it reads the
system, resource, display, interface, device, and runtime snapshots, plus the
current network state. It arranges six panels according to the active screen
size.

On a typical 1920x1080 display, the dashboard has three columns and looks like
this. The values below are examples; the application reads the real values
from the device when it starts.

```text
┌─────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    IoT App | Running | 14:32:07                                     │
│                             raspberrypi | Default app | Uptime 01:42:18                             │
├─────────────────────────────────┬─────────────────────────────────┬─────────────────────────────────┤
│             System              │             Network             │             Display             │
│                                 │                                 │                                 │
│     Raspberry Pi 4 Model B      │         eth0: Connected         │      Connected displays: 1      │
│         Raspberry Pi OS         │        IPv4: 192.0.2.10         │      HDMI-A-1, AOC U27B3A       │
│       Linux 6.12, aarch64       │        Speed: 1000 Mbps         │        1920x1080 @ 60 Hz        │
├─────────────────────────────────┼─────────────────────────────────┼─────────────────────────────────┤
│      Resources at startup       │           Interfaces            │             Devices             │
│                                 │                                 │                                 │
│       CPU: 4 cores, 43 C        │        I2C interfaces: 1        │         USB devices: 4          │
│           Load: 0.18            │       GPIO controllers: 2       │        Input devices: 3         │
│     Memory: 436 MB / 1.8 GB     │        SPI interfaces: 0        │        Block devices: 2         │
│    Storage: 3.2 GB / 14.5 GB    │      Serial interfaces: 2       │                                 │
├─────────────────────────────────┴─────────────────────────────────┴─────────────────────────────────┤
│                           IoT App 0.1.0 | MicroPython 1.28.0 | LVGL 9.5.0                           │
└─────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

On a screen narrower than 1000 pixels, the same six panels are arranged as two
columns and three rows.

One scheduler callback updates the header every second using the current local
time and live uptime. A second callback reads the network interfaces every five
seconds and updates the existing Network panel. Resource and device panels
remain startup snapshots, so the dashboard does not repeatedly scan every
Linux value.

The dashboard does not create a gamepad or scan I2C addresses. User hardware
belongs to the application that knows its bus and address.

## 13. MicroPython subsystem

### 13.1 How the Python components fit together

The classes under `include/iot/python` each handle one part of a Python
application's lifetime:

| Component | What it does |
|---|---|
| `ApplicationMetadata` | Holds the `id`, `name`, and `entry_point` values read from `app.json`. |
| `PythonApplication` | Holds a completely loaded application, including its metadata, paths, and Python source code. This is the object passed through the rest of the runtime. |
| `PythonApplicationLoader` | Reads `app.json` and the entry-point file from an application directory, checks them, and returns a `PythonApplication`. |
| TemporaryPythonApplicationInstaller | Writes an application that passed MQTT validation under /tmp and returns it with its final paths. The shipped default application does not use this installer. |
| PythonApplicationManager | Owns one Python application session. It creates and destroys the context and interpreter, advances timers, tracks application state, and shows the C++ emergency screen after a failure. |
| `MicroPythonApplicationContext` | Gives the display and system bridge functions access to the C++ services used by the current Python application. |
| `MicroPythonRuntime` | Allocates the Python heap, starts MicroPython, executes `main.py`, runs scheduled callbacks, and shuts the interpreter down. |

The same headers also define a few small values passed between these classes:

- `ApplicationState` says whether the default app, an external app, the
  emergency screen, or nothing is active.
- `PythonExecutionResult` carries success or a Python traceback from the
  runtime to the manager.
- `ExternalApplicationActivationResult` tells the deployment controller
  whether an external app started.

The shipped default application follows this path when `iot_app` starts:

```text
main()
  |
  v
PythonApplicationLoader
  |  reads app.json and main.py
  v
PythonApplication
  |
  v
PythonApplicationManager
  |
  ├── creates MicroPythonApplicationContext
  └── creates MicroPythonRuntime -> executes main.py
```

An application received through MQTT has two extra steps at the beginning:

```text
ApplicationDeploymentController
  |
  v
TemporaryPythonApplicationInstaller
  |  writes the checked application under /tmp
  |
  v
PythonApplication
  |
  v
PythonApplicationManager
```

After this point, both application types use the same manager, context, and
MicroPython runtime.

While an application is running, the main loop asks the manager when the next
Python callback is due. The manager asks the runtime directly:

```text
main loop
  |
  v
PythonApplicationManager
  |
  v
MicroPythonRuntime -> MicroPython scheduler
```

Python display and system calls use a different path. Their C++ bridge files
get the active context and use it to reach the required C++ service:

```text
Python application
  |
  v
Native MicroPython module
  |
  v
display_cpp_bridge.cpp or system_cpp_bridge.cpp
  |
  v
MicroPythonApplicationContext
  |
  ├── ScreenManager
  ├── display information
  └── system information
```

The input bridge does not use this context. Each Python gamepad object has its
own C++ gamepad handle, so its bridge functions can use that handle directly.

When an application stops, fails, or is replaced, the manager destroys the
interpreter before the context:

```text
1. Destroy MicroPythonRuntime
   MicroPython stops and releases its heap.

2. Destroy MicroPythonApplicationContext
   The bridge functions can no longer reach application services.
```

This order keeps the context available while MicroPython is shutting down.
`ScreenManager` is not destroyed during an application switch because the next
application or the emergency screen uses the same display service.

If Python startup or a scheduled callback fails, MicroPythonRuntime returns
the traceback to PythonApplicationManager. The manager stops the interpreter,
builds the error text, and sends it to the still-running ScreenManager. The
failure details are no longer needed after the screen command has copied the
completed text.

### 13.2 Starting an interpreter

Each Python application gets a new MicroPython interpreter. This prevents
variables, timers, and other Python state from the previous application from
being reused accidentally.

PythonApplicationManager creates two objects for each application:

- `MicroPythonApplicationContext` connects Python modules to the C++ services
  and device information they need.
- `MicroPythonRuntime` owns the interpreter and its fixed-size memory area.

The manager creates the context before the interpreter and destroys it after
the interpreter. The C++ services are therefore still available while
MicroPython shuts down. Only the main thread runs the interpreter.

### 13.3 Running the Python entry point

The application source follows this simple path:

```text
Python source -> compile -> run
                            |
                            +--> success: keep the interpreter running
                            |
                            +--> error: save the traceback and show the emergency screen
```

A small C helper compiles and runs the source. MicroPython reports some errors
with a non-local jump, so the helper catches them before returning to C++. That
prevents the jump from skipping live C++ objects and their destructors.

The runtime keeps up to 8 KiB of traceback text. It uses that text for logs and
the error screen. If the entry point finishes without an error, the interpreter
stays alive so its global objects and scheduled callbacks can continue working.

### 13.4 Giving Python access to C++ services

The native `iot` Python modules need services owned by C++. For example,
`iot.display` needs `ScreenManager` to draw on the screen.

`MicroPythonApplicationContext` is the connection between them. It gives the
currently running application access to:

- Screen drawing
- The active display and monitor details captured at startup
- System information and live uptime
- The current application name

The process runs only one Python application at a time, so only one context can
be active.

### 13.5 Native module boundary

The public Python module is `iot`. It exposes four private native
implementations under stable names:

```text
Python application
       |
       v
mod_iot.c                         public module: iot
       |
       v
native modules
├── mod_iot_display.c
├── mod_iot_input.c
├── mod_iot_scheduler.c
└── mod_iot_system.c
       |
       v
C-compatible bridge (`extern "C"` and `runSafely()`)
       |
       v
C++ services
```

The bridge is needed because the two sides use different programming models:

- MicroPython's native-module API is C and works with values such as
  `mp_obj_t`.
- The IoT App services are C++ classes such as `ScreenManager` and
  `AdafruitMiniI2cGamepad`.
- C code cannot call C++ class methods directly or handle a C++ exception.

Each bridge function is declared with `extern "C"` so C and C++ agree on its
compiled name.

The C module calls a function using the plain name written in its source. For
example, compiling `mod_iot_display.c` creates a request for this name:

```text
iot_display_clear
```

C++ normally adds encoded type information to a compiled function name. This
allows C++ to have several functions with the same source name but different
parameters. Depending on the compiler, the C++ implementation could therefore
be stored under a name similar to:

```text
_Z17iot_display_clearhhh
```

The linker only matches compiled names. It would see this mismatch:

```text
C module requests:       iot_display_clear
C++ object provides:     _Z17iot_display_clearhhh
                         Names do not match -> link fails
```

`extern "C"` tells the C++ compiler to keep the bridge function's compiled
name in the C form:

```text
C module requests:       iot_display_clear
C++ object provides:     iot_display_clear
                         Names match -> link succeeds
```

This changes only the name and calling linkage seen by the linker. It does not
turn the implementation into C. The function body can still use normal C++
classes, lambdas, and exception handling.

A display call follows this path:

```text
Python: display.clear(...)
  |
  v
mod_iot_display.c
  Parse Python arguments into red, green, and blue integer values.
  |
  v
iot_display_clear(...)                     extern "C" bridge function
  |
  v
runSafely([=] { ... })                     C++ exception boundary
  |
  v
ScreenManager::clear(...)
```

`runSafely()` is important because a C++ exception must not continue into
MicroPython's C code. It runs the C++ work inside `try` and `catch` blocks and
returns a plain C structure:

```text
C++ call succeeds
  -> { succeeded = 1, error_message = null }

C++ call throws an exception
  -> runSafely() catches it
  -> { succeeded = 0, error_message = "..." }
```

The error text is kept in thread-local storage long enough for the C binding to
read it. After the bridge has returned to C, the binding converts a failed
result into a MicroPython `RuntimeError`. This order means neither exception
system crosses into code that does not understand it.

The opposite direction is also protected. MicroPython startup and scheduled
callback errors are caught in C before control returns to C++. This prevents
MicroPython's non-local exception jump from skipping live C++ objects and their
destructors.

The bridge headers contain only C-compatible values, pointers, and structures.
MicroPython argument handling stays in the small C files, while the hardware,
display, and application logic remains in the C++ classes.

### 13.6 Enabled MicroPython features

The embed configuration enables the compiler, garbage collection, finalizers,
source line numbers, Python `sys`, floating-point values, and 64-bit integer
support.

External imports and the normal Python I/O module are disabled. The current
deployment protocol sends one entry-point file, so an app cannot import extra
package files from disk.

## 14. Native modules

This section explains each module's place in the system. The exact parameters
and examples are in the [MicroPython API guide](../micropython-api/README.md).

### 14.1 `iot.display`

The display binding converts Python values into `TextBoxSpec`, `FilledAreaSpec`,
and screen commands. It does not call LVGL itself. Defaults for text styling
live in the C++ `TextBoxSpec`; the binding changes only values supplied by
Python.

Text creation returns a widget ID. Python uses that ID to update, move, or
delete the same LVGL object later.

### 14.2 `iot.scheduler`

The scheduler stores repeating tasks in the MicroPython VM. Each task contains
a callback, positive ID, interval, and remaining time.

When C++ asks for the next delay, the scheduler returns the shortest remaining
time. When that time has passed, it gathers the due callbacks and then runs
them one by one. Gathering first means a callback can add or cancel timers
without changing the list currently being walked.

If several intervals were missed, a repeating callback runs once rather than
being replayed many times in a burst. Its next deadline stays aligned with its
interval.

### 14.3 `iot.system`

Most values come from a snapshot taken when the application starts. The
snapshot includes machine, resource, interface, and device details. This avoids
repeated full scans and gives those dashboard panels a consistent view.

Current local time, uptime, and network interfaces are live calls because those
values can change while a screen is running.

### 14.4 `iot.input`

The input binding currently exposes the Adafruit Mini I2C STEMMA QT Gamepad.
Python owns a C++ gamepad object through an opaque handle. Joystick and button
view objects keep their Python gamepad owner alive.

The binding requires the I2C bus and address. The default
dashboard does not guess an address or probe every I2C device.
`gamepad.connection_information()` returns the selected bus, address, and
Linux device path for diagnostics without contacting the hardware again.

## 15. Python scheduler and application updates

A Python application performs setup and registers callbacks:

```python
from iot import display, scheduler, system

clock = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=80,
    text=system.current_time(),
)

def update_clock():
    display.update_text_box(clock, system.current_time())

scheduler.every(milliseconds=1000, callback=update_clock)
```

The callback runs on the main thread. Its display request is copied into the
render queue and handled on the render thread.

A callback should do one short piece of work and return. A permanent loop or a
long sleep delays other timers and deployment processing.

## 16. Application state and failure handling

`PythonApplicationManager` makes the current state explicit:

```text
Stopped
   |
   | iot_app starts
   v
Start the shipped default application
   |
   +--> startup succeeds --> DefaultApplication
   |
   +--> startup fails ----> EmergencyScreen


A valid external deployment may arrive while the current state is:
├── DefaultApplication
├── ExternalApplication
└── EmergencyScreen
        |
        v
Stop the current Python app, if one is running, and start the external app
        |
        +--> startup succeeds --> ExternalApplication
        |
        +--> startup fails ----> EmergencyScreen


DefaultApplication or ExternalApplication
   |
   | scheduled callback fails
   v
EmergencyScreen


EmergencyScreen remains until:
├── a valid external deployment begins activation
└── iot_app restarts and starts the shipped default application

Any state -- iot_app stops --> Stopped
```

If a new external application fails during startup, its traceback replaces the
previous error and the state returns to `EmergencyScreen`.

The shipped default application starts only when the `iot_app` process starts.
After an external application replaces it, the default application does not
run again during that process. Any Python application failure stops the
interpreter and shows the C++ emergency screen. A later external deployment can
still replace the emergency screen. Restarting `iot_app` starts the shipped
default application again.

A deployment rejected during message validation or temporary installation does
not reach `PythonApplicationManager`. It leaves the current application state
and screen unchanged.

The emergency screen is drawn by C++ and does not need a Python application.
If the display system itself stops working, IoT App cannot draw any screen. It
logs the display error and stops normal execution.

There are four states:

| State | Meaning |
|---|---|
| `Stopped` | No interpreter is running |
| `DefaultApplication` | The shipped app is running |
| `ExternalApplication` | An MQTT-delivered app is running |
| `EmergencyScreen` | No Python app is running; C++ displays the failure |

### 16.1 Starting the default app

During process startup, the manager clears the screen and starts the shipped
default application. This startup step happens once. If the default application
raises an exception, the manager records the failure and shows the C++
emergency screen.

### 16.2 Starting an external app

The manager stops the current app before starting the new one. This guarantees
that there is never more than one MicroPython context.

If startup succeeds, the state becomes `ExternalApplication` and an earlier
failure record is cleared.

If startup fails, the manager records the traceback, stops the failed
interpreter, and shows the red native emergency screen. It does not restart the
shipped default application.

### 16.3 Scheduled callback failure

When a callback in either the default or an external application raises an
unhandled exception, the interpreter is stopped and the native emergency
screen shows the traceback. No Python application continues running.

The emergency screen is implemented entirely in C++. It does not need a Python
application. It stays visible until a new external app starts or the `iot_app`
process restarts.

### 16.4 Why the error screen is native

The emergency screen belongs to C++, so a broken Python application cannot
remove the only available traceback display. C++ clears the application's
widgets before drawing the error.

## 17. MQTT deployment subsystem

### 17.1 Ubuntu sender

`iot_app_sender/send_app.py` is the development-side command-line tool. Its
JSON configuration identifies the target device, MQTT broker, and application
directory. The sender reads `app.json` and finds the Python entry point itself.

The sender:

1. Reads and validates the local application metadata and Python source.
2. Creates a unique transfer ID.
3. Calculates source size and SHA-256, then Base64-encodes the source.
4. Builds the install and transfer-specific status topics.
5. Connects to the broker with MQTT 5.
6. Subscribes to the status topic before publishing the install request. This
   prevents a fast device reply from being missed.
7. Publishes the request at QoS 1 without retaining it.
8. Prints each device status and returns success only for final status
   `started`.

`--dry-run` builds and validates the message without connecting. `--no-wait`
returns after broker acknowledgement instead of waiting for the device's final
result.

A **transfer ID** identifies one attempt to send an application. The sender
creates a new value for every send operation, even when it sends the same
application again. For example:

```text
71b84271630a467aa16ee7b4a0c39632
```

The transfer ID lets the sender match status replies to the correct request.
The device also uses it to recognize a duplicate MQTT delivery and to name the
application's temporary installation directory. It is different from the
application ID in `app.json`, which identifies the application itself.

### 17.2 Topics

MQTT uses one topic to send an application to the Raspberry Pi and another
topic to return its status.

The Ubuntu sender publishes the application to this device-specific install
topic:

```text
iot/devices/raspberrypi-01/applications/install
```

Here, `raspberrypi-01` is the device ID. Only the IoT App process using that
device ID subscribes to this topic.

IoT App includes the transfer ID in the status topic:

```text
iot/devices/raspberrypi-01/applications/status/<transfer-id>
```

For example, a transfer ID of `abc123` produces:

```text
iot/devices/raspberrypi-01/applications/status/abc123
```

The sender subscribes to this status topic before publishing the application.
It can therefore receive progress and the final result for that specific
transfer.

Both sides calculate this topic from `device_id` and `transfer_id`. It is not
included in the JSON message or sent as an MQTT Response Topic property. There
is only one rule for finding the status topic.

Both directions use MQTT QoS 1. This asks the broker to deliver each message at
least once, although a message may occasionally be delivered more than once.
The transfer ID in the status topic and JSON payload is enough to match a reply
to its request, so the protocol does not add a second correlation property.

### 17.3 Deployment message

The sender creates one JSON message containing:

- Message type
- Transfer ID and target device ID
- Application metadata from `app.json`
- Base64-encoded entry-point source
- Original source byte count
- SHA-256 of the decoded source

The current message format supports one Python source file. It keeps
`entry_point` in the application metadata only; the source object does not
repeat it.

### 17.4 Passing a received message to the main thread

Libmosquitto calls `MqttApplicationReceiver::handleMessage()` on its network
thread when an application message arrives. This function checks the topic,
payload, and total message size. It then copies the JSON text into the bounded
`ApplicationMessageQueue` and returns.

The main thread waits in `ApplicationMessageQueue::waitAndPopMessage()`. When a
message is available, the main thread removes it from the queue and passes it
to `ApplicationDeploymentController::process()`. The controller can then parse
the JSON, install the application, and start MicroPython.

`MqttApplicationReceiver` owns the libmosquitto client, callbacks, connection,
subscription, and publication work. Other components use the message queue or
the status-publishing interface instead of calling libmosquitto directly.

The MQTT callback can arrive at any time. It puts the message in the queue and
returns without calling MicroPython. The main thread later removes the message
and does the MicroPython work on the thread that owns the interpreter.

### 17.5 Validation

`ApplicationDeploymentMessageParser` checks:

- The payload is one complete JSON object.
- Required fields are present once and have the expected type.
- Message type is `install_single_file_application`.
- Transfer ID uses safe characters and length.
- Device ID matches this device.
- Application metadata follows the same rules as a local package.
- Encoding is Base64.
- Decoded source is not empty and has no null byte.
- Decoded size matches `size_bytes` and the configured source limit.
- SHA-256 matches the decoded source.

SHA-256 detects damage or an inconsistent payload. It is not a digital
signature and does not prove who sent the application.

### 17.6 Temporary installation

After validation, `TemporaryPythonApplicationInstaller` works under:

```text
/tmp/iot-app-<user-id>/applications/
```

The installation steps are:

1. Create a private staging directory with user-only permissions.
2. Write a generated `app.json` and the entry-point source as private files.
3. Rename staging to the transfer directory.
4. Return a `PythonApplication` built from the validated metadata, source, and
   final paths.
5. Remove staging if any step fails.

Renaming keeps the final directory from appearing half-written. The installer
clears old temporary applications when IoT App starts.

After a new deployment is processed, the previous external application's
temporary directory is removed. A failed new application is also removed.

### 17.7 Deployment status

The controller publishes progress as it works:

```text
received
validating
starting
started
```

Possible final failure states are:

```text
rejected
failed
```

`rejected` means the message did not pass validation. `failed` means a later
installation or startup step could not complete. A startup failure leaves the
native emergency screen visible; it does not restart the default application.

MQTT QoS 1 may deliver the same install message more than once. The controller
remembers a bounded number of final results by transfer ID. A duplicate gets
the saved final answer instead of starting the same app again.

### 17.8 Reading the sender output

A successful deployment produces output similar to this:

```text
Application: .../sample_applications/moving_text_in_frame
Python source: .../sample_applications/moving_text_in_frame/main.py
MQTT broker: rspi-iot-app.local:1883
Install topic: iot/devices/raspberrypi-01/applications/install
Transfer ID: 71b84271630a467aa16ee7b4a0c39632
Message size: 6657 bytes
The MQTT broker acknowledged the deployment message.
Device status: received: Message received by IoT App
Device status: validating: Source size and SHA-256 are valid
Device status: starting: Temporary application is valid and is starting
Device status: started: External application started successfully
```

The lines before the broker acknowledgement describe the request prepared by
the Ubuntu sender. The acknowledgement means only that the MQTT broker
received the install message. It does not mean that the Raspberry Pi accepted
or started the application.

Every `Device status` line comes from a separate MQTT status message published
by IoT App on the Raspberry Pi:

| Status | Meaning |
|---|---|
| `received` | IoT App received the message and started processing its transfer ID. |
| `validating` | The JSON fields, device ID, application metadata, source size, Base64 data, and SHA-256 passed validation. |
| `starting` | The temporary application was written and loaded, and IoT App is about to run its Python entry point. |
| `started` | The entry point finished without an unhandled startup exception. The MicroPython interpreter remains active for objects and scheduled callbacks. |
| `rejected` | Message validation failed. The currently displayed app or emergency screen is left unchanged. |
| `failed` | Temporary installation or Python startup failed. If Python startup was attempted, IoT App shows the native emergency screen. |

For example, a Python exception during startup ends with `failed`:

```text
Device status: received: Message received by IoT App
Device status: validating: Source size and SHA-256 are valid
Device status: starting: Temporary application is valid and is starting
Device status: failed: Python raised an exception while starting the external application
```

The final MQTT reply for that startup failure is:

```json
{
  "transfer_id": "<transfer-id>",
  "status": "failed",
  "application_id": "<application-id>",
  "message": "Python raised an exception while starting the external application"
}
```

The MQTT reply does not contain the Python traceback. IoT App writes the full
traceback to the Raspberry Pi log and shows it on the native emergency screen.
No default application is restored. The emergency screen stays visible until
another valid external application is sent or `iot_app` restarts. The Ubuntu
sender exits with code `2` because the deployment did not reach `started`.

The status message itself is JSON. The sender reads it and prints the shorter
`Device status` line:

```json
{
  "transfer_id": "71b84271630a467aa16ee7b4a0c39632",
  "status": "started",
  "application_id": "moving-text-in-frame",
  "message": "External application started successfully"
}
```

The sender ignores a status message whose `transfer_id` does not match the
request it is waiting for.

#### Startup failure compared with a later callback failure

The sender result depends on when the Python exception happens:

| When Python fails | What the Ubuntu sender reports | What the Raspberry Pi shows |
|---|---|---|
| While running the entry point during startup | `failed: Python raised an exception while starting the external application` | The native emergency screen shows the startup traceback. No Python app remains running. |
| Later, inside a scheduled callback | The sender has already received `started: External application started successfully` | IoT App stops Python and the native emergency screen shows the callback traceback. |

A scheduled callback cannot run until the entry point has finished. The
`started` status confirms successful startup, but it cannot predict whether a
later callback will fail.

The current protocol does not publish a second MQTT status when a later
callback fails. The sender may therefore finish successfully before the
Raspberry Pi changes to the emergency screen. The traceback is still written
to the Raspberry Pi log.

## 18. System information subsystem

`LinuxSystemInformationProvider` reads information from normal Linux APIs and
small files:

| Information | Source |
|---|---|
| Hostname | `gethostname()` |
| Device model | device tree under `/proc` or `/sys` |
| Operating system | `/etc/os-release` |
| Kernel and architecture | `uname()` |
| CPU count | `sysconf()` |
| CPU temperature | thermal zones under `/sys/class/thermal` |
| Load average | `getloadavg()` |
| Uptime and memory | `sysinfo()` and `/proc/meminfo` |
| Root storage | `statvfs("/")` |
| Network interfaces and IPv4 | `getifaddrs()` |
| Link speed | `/sys/class/net/<name>/speed` |
| Interface and device counts | `/dev` and `/sys/class` scans |

Missing optional values do not fail the complete dashboard. Text values use
`Unavailable`; optional numeric values become Python `None`.

The I2C count reports interfaces such as `/dev/i2c-1`. It does not scan bus
addresses. Blind I2C scanning can send unsafe commands to unknown devices, so
the system summary avoids it.

## 19. I2C and gamepad subsystem

### 19.1 Linux I2C transport

`I2cDevice` owns one `/dev/i2c-N` file descriptor and one selected seven-bit
address. It validates bus and address ranges, reads adapter capabilities, and
closes the descriptor through RAII.

`I2cDevice` owns the Linux operations needed to open the bus, select an address,
and transfer bytes. Hardware drivers use `II2cDevice` and do not call Linux
`open()`, `ioctl()`, `read()`, `write()`, or `close()` themselves.

It supports:

- One write transfer
- One read transfer
- Combined write/read using a repeated start
- Write, delay, then read while holding the same device lock

The per-device mutex prevents two callers from mixing parts of a transaction.
Transfer sizes are checked against the Linux 16-bit I2C message length.

### 19.2 Hardware-independent controller model

`GameController` defines the common behavior expected from future controller
drivers. It stores a `GamepadJoystick` and `GamepadButtons` state independent of
whether the hardware uses I2C, USB, or another transport.

The current `AdafruitMiniI2cGamepad` implements that interface. It talks to the
bus through `II2cDevice`; the real application supplies `I2cDevice`. A future
vendor driver can implement the same controller contract without changing
application concepts such as direction and pressed buttons.

### 19.3 Adafruit gamepad flow

Connecting the gamepad performs these steps:

1. Reset the Seesaw processor.
2. Read the hardware ID.
3. Read the combined product ID and firmware date code.
4. Confirm product ID 5743.
5. Configure all six button inputs with pull-ups.
6. Read the initial buttons and joystick position.

The buttons are active-low: an electrical zero means pressed. The driver turns
the hardware input bits into the logical `X`, `Y`, `A`, `B`, `Select`, and
`Start` values used by the rest of the app.

Each joystick axis is a 10-bit value from 0 to 1023. The driver reverses the raw
orientation so larger X means right and larger Y means up. Calibration averages
several resting samples and stores the measured centre and dead zone.

`refreshInputState()` performs the I2C work. Joystick and button accessors read
the cached result, which lets several values describe the same hardware sample.

## 20. Runtime configuration

The executable accepts only `--help`. Runtime behavior uses central defaults
and these environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `IOT_DEVICE_ID` | `raspberrypi-01` | Device name used in MQTT topics |
| `IOT_MQTT_HOST` | `127.0.0.1` | MQTT broker address |
| `IOT_MQTT_PORT` | `1883` | MQTT broker port |
| `IOT_MQTT_USERNAME` | empty | Optional MQTT username |
| `IOT_MQTT_PASSWORD` | empty | Optional MQTT password |

The display connector and default application are not command-line choices.
The runtime follows its fixed display-selection policy and always begins with
the shipped default app.

### 20.1 Bounded resources

The default runtime limits are:

| Resource | Default limit |
|---|---:|
| Python entry-point source | 512 KiB |
| MicroPython heap | 1 MiB |
| MQTT message | 1 MiB |
| Queued deployment messages | 4 |
| Remembered deployment results | 64 |
| Pending render commands | 256 |
| Active Python timers | 128 |
| Captured Python traceback | 8 KiB |
| Application metadata in `app.json` | 64 KiB |

These limits stop a fast producer or broken application from growing the main
queues and heaps without control.

## 21. Security model

Received Python is currently treated as trusted code. The source-size limit,
fixed Python heap, bounded queues, path validation, and SHA-256 checks improve
robustness, but they do not create a security boundary.

## 22. Failure handling

| Failure | Current response |
|---|---|
| No connected display | Stop startup with an error |
| Framebuffer and active DRM size differ | Stop startup with an explanation |
| Default package is missing or invalid | Stop startup before Python begins |
| Default Python startup fails | Show native emergency screen |
| MQTT cannot start | Log the error and keep the default app running |
| MQTT message queue is full | Drop the new message and log it |
| Render command queue is full | Raise an error to the calling application |
| Render thread fails | Main loop rethrows and stops the process |
| Deployment message is invalid | Publish `rejected` when a safe transfer ID exists |
| External app startup fails | Stop Python and show native emergency screen |
| External callback fails | Stop Python and show native emergency screen |
| Default callback fails | Stop Python and show native emergency screen |

An invalid MQTT message without a safe transfer ID cannot be matched to a safe
transfer-specific status topic. It is logged and dropped.

## 23. Raspberry Pi OS deployment

During development, the executable runs directly on Raspberry Pi OS. It should
run from console mode because a desktop compositor may redraw over `/dev/fb0`.

The runtime user needs access to:

- `/dev/fb0`
- Primary DRM devices under `/dev/dri`
- Any I2C buses used by Python applications
- Input devices when later modules need them

Typical groups are `video`, `render`, `i2c`, and `input`.

## 24. Buildroot integration

`iot_app/buildroot_external` is a BR2_EXTERNAL tree. It keeps project-owned
Buildroot configuration outside the Buildroot submodule.

The package recipe:

- Builds IoT App with the CMake package infrastructure.
- Points CMake at the root LVGL and MicroPython submodules.
- Selects cJSON, libdrm, Mosquitto, and OpenSSL dependencies.
- Creates a dedicated `iot-app` runtime user.
- Adds the user to `video`, `render`, `i2c`, and `input` groups.
- Installs either a SysV startup script or systemd service.
- Installs the shared launcher, storage helper, cursor helper, and Mosquitto
  configuration from `iot_app/image_support`.
- Installs the shared device-access rules for systemd/udev builds.

### Why helper programs are installed under `/usr/libexec`

`/usr/bin` is used for programs that a user may run directly. The main
application is therefore installed as:

```text
/usr/bin/iot_app
```

`/usr/libexec` is commonly used for executable helper programs that belong to
an application or service. These files are executable programs, not shared
libraries. They are kept out of `/usr/bin` because users do not normally call
them directly. The startup scripts and systemd services use their complete
paths instead of looking for them through `PATH`.

Buildroot and Yocto create `/usr/libexec` while installing their service
helpers. Both install the same launcher, storage helper, and cursor helper:

| Helper | Purpose |
|---|---|
| `/usr/libexec/iot-app-launcher` | Chooses the development executable from `/data`, or uses `/usr/bin/iot_app` |
| `/usr/libexec/iot-app-prepare-data-storage` | Expands, mounts, and prepares the persistent `/data` partition |
| `/usr/libexec/iot-app-hide-tty1-cursor` | Hides the tty1 console cursor before the framebuffer dashboard starts |

The GNU build-system documentation describes
[`libexecdir`](https://www.gnu.org/prep/standards/html_node/Directory-Variables.html#index-libexecdir)
as the location for executable programs intended to be run by other programs
rather than by users.

The external tree contains `iot_rpi4_defconfig`, which builds the Raspberry Pi
4 Model B development image described in this document.

The framebuffer is the only service dependency required before IoT App can
start. Systemd expresses that dependency through `dev-fb0.device`. The
Buildroot SysV script starts near the end of boot and waits up to 10 seconds
only if `/dev/fb0` has not appeared yet. Neither service waits for an IP
address or a working MQTT connection. The default dashboard can run offline,
and libmosquitto reconnects when the local broker becomes available. The SysV
script uses `start-stop-daemon` with the same runtime account.

The Raspberry Pi 4 image mounts persistent storage at `/data`. During normal
startup, `/usr/libexec/iot-app-launcher` checks for
`/data/iot-app/development/iot_app`. A regular executable at that path is used
for development testing. Otherwise, the launcher runs `/usr/bin/iot_app` from
the image. Removing the development file restores the installed executable on
the next service start.

## 25. Yocto integration

`meta-iot-app` is the project-owned Yocto layer. The upstream Poky,
OpenEmbedded, and Raspberry Pi layers remain unmodified submodules.

The application recipe builds the same `iot_app` CMake target used by native
and Buildroot builds. It points CMake at the pinned LVGL and MicroPython source
trees, creates the `iot-app` account, installs the systemd service, and adds
the shared launcher, cursor helper, and device-access rules.

The system configuration package installs the Ethernet and Wi-Fi network
units, enables network and time services, reserves `tty1` for the dashboard,
and provides an emergency login on `tty2`.
The root `wpa_supplicant.conf` is the private Wi-Fi configuration shared by
Buildroot and Yocto. The preparation command copies it into the Yocto build
directory, and an append file installs that copy in the image. A separate
append file installs the shared development Mosquitto configuration without
making two packages own the same file.

`iot_app/image_support` is the common source for the launcher, `/data` helper,
cursor helper, Mosquitto configuration, and device-access rules. Buildroot and
Yocto install those same files through their own recipes. Their startup
services and partition descriptions remain separate because the two build
systems use different formats.

`iot-app-image.bb` creates the complete Raspberry Pi image. It includes SSH,
Mosquitto, Wi-Fi firmware, I2C tools, time-zone information, and the IoT App
packages. The generated Wic image contains boot, root, and expandable data
partitions and can be written to a microSD card with Raspberry Pi Imager.

## 26. Source layout by responsibility

```text
src/runtime/
  process startup, configuration, and main loop

src/platform/linux/
  DRM display discovery, EDID, I2C, and Linux system information

src/ui/
  ScreenManager and LVGL framebuffer backend

src/input/
  common controller state and Adafruit gamepad protocol

src/python/
  application loading, interpreter ownership, supervision, and failure handling

src/messaging/
  MQTT receiving, deployment parsing, queueing, status, and activation

micropython_iot_modules/
  thin MicroPython C bindings and C-to-C++ bridges

default_python_application/
  shipped default dashboard

buildroot_external/
  final image package, service, users, permissions, and board configuration
```


## 26. Important system rules

These rules keep the current design predictable:

1. Only the render thread calls LVGL.
2. Only the main thread calls MicroPython.
3. MQTT callbacks only copy bounded messages into the queue.
4. One Python interpreter and one native context exist at a time.
5. The screen manager outlives every Python application.
6. A new app starts with a clean interpreter and cleared screen.
7. Received source stays in `/tmp` and the shipped default stays persistent.
8. Shipped apps are loaded from disk; received apps use the metadata and source
   already checked by the deployment parser.
9. The active Linux display mode is read, never changed.
10. Failure display does not depend on Python drawing code.
