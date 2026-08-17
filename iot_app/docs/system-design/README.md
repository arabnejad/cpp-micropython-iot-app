# IoT App system design

## 1. Purpose

IoT App is a C++ program for an embedded Linux device such as a Raspberry Pi.
It owns the display, embeds MicroPython, and gives Python applications a small
set of native APIs for drawing, downloading files, reading system information,
scheduling work, and using supported hardware.

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

- Scan the connected monitors once, then select the monitor and mode used for
  drawing.
- Draw directly to the Linux framebuffer with LVGL.
- Download bounded HTTP or HTTPS files into a temporary directory.
- Decode, scale, cache, and display JPEG images.
- Run one MicroPython application at a time.
- Expose project-owned native modules as `iot.display`, `iot.input`,
  `iot.network`, `iot.scheduler`, and `iot.system`.
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

`ApplicationDeploymentService` owns the receiver, queue, and deployment
controller shown above. `main.cpp` starts this service and asks it to process
one queued message at a time. The three owned classes remain separate because
receiving MQTT data, crossing a thread boundary, and processing a deployment
are different jobs.

The deployment result travels back separately:

```text
Raspberry Pi -- success or failure --> MQTT broker --> Ubuntu sender
```

The main thread controls the process. It starts and stops Python, validates
deployments, and runs scheduled callbacks. The MQTT thread never starts an
application, and the render thread never runs Python. Each thread therefore
uses only the library and state it owns.

### 3.1 Application deployment pipeline

`main.cpp` uses `ApplicationDeploymentService` as the single entry point for
MQTT application deployment. Inside the service, each received message follows
this pipeline:

```text
ApplicationDeploymentService
    |
    +-- MQTT network thread
    |     |
    |     v
    |   MqttApplicationReceiver::handleMessage()
    |     |  checks the topic, payload, and message size
    |     |  copies the raw JSON with tryPush()
    |     v
    |   ApplicationMessageQueue
    |
    +-- Main thread calls waitForAndProcessOneMessage()
          |
          |  removes one message with waitAndPopMessage()
          v
        ApplicationDeploymentController::process()
          |
          +--> ApplicationDeploymentMessageParser
          |      checks the JSON and reads the application
          |
          +--> TemporaryPythonApplicationInstaller
          |      writes the checked application under /tmp
          |
          +--> PythonApplicationManager
          |      replaces the running Python application
          |
          +--> MqttApplicationReceiver::publishStatus()
                 sends progress or an error back to the sender
```

The queue only holds received JSON while it waits for the main thread. It is
not a list of installed applications and it does not understand the JSON. The
parser runs later, when the main thread calls the deployment controller.

`ApplicationDeploymentService` owns the pipeline and provides its start, stop,
and main-loop operations. `ApplicationDeploymentController` coordinates the
main-thread work. The other classes continue to handle MQTT, thread
communication, validation, temporary files, and Python execution separately.
This avoids one large class that would need to understand all of those jobs.

The controller also publishes progress and any validation or installation
error. Its final `accepted` reply confirms the package is installed, not that
Python compiled or ran successfully.

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
without a limit. The render thread handles up to 16 commands at a time, then
gives LVGL a chance to refresh the display. This still happens when new drawing
commands keep arriving.

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
  JPEG decoding and decoded-pixel cache
  system information
  I2C transport
  game controller support

iot_runtime
  embedded MicroPython
  native Python modules
  application loading and supervision
  HTTP and HTTPS downloads
  scheduler
  MQTT receiving and deployment

iot_app
  runtime configuration
  main() and process lifecycle
```

`iot_runtime` publicly links `iot_platform`, so the `iot_app` executable only
needs to link `iot_runtime`. CMake carries the platform dependency through
automatically. There is no library for every directory. The two-library split
is enough to separate Linux and hardware support from application-runtime
behavior.

### 5.1 Third-party components

| Component | Use in this project |
|---|---|
| MicroPython | Runs the shipped or received Python application inside the C++ process. It is compiled into `iot_app`, so the target system does not need a separate Python installation. Each application starts with a new interpreter. |
| LVGL | Creates the on-screen widgets and draws them through the Linux framebuffer. Only the render thread calls LVGL because the project does not treat it as thread-safe. |
| libdrm | Finds connected displays and reads their connector, EDID, and active-mode information. It does not render the UI or change the display resolution. |
| libmosquitto | Connects to the MQTT 5 broker, receives application-install messages, and publishes deployment results. Its network callbacks place received work in a queue for the main thread. |
| cJSON | Reads application metadata and incoming deployment JSON, and creates the JSON used for deployment status replies. |
| OpenSSL Crypto | Decodes the Base64 Python source carried in JSON. One internal checksum helper uses it to calculate SHA-256 for deployments and downloaded files. Base64 is only an encoding, and SHA-256 only detects inconsistent or damaged content; neither one proves who sent the application. |
| libcurl | Downloads HTTP and HTTPS files. Certificate and hostname checks remain enabled for HTTPS. |
| libjpeg-turbo | Checks, scales, and decodes JPEG files before their pixels are sent to LVGL. |

MicroPython and LVGL are pinned repository submodules. Project code does not
modify those source trees. CMake generates the MicroPython embed sources into
the build directory and compiles them into `iot_runtime`. LVGL is also compiled
into the application. The target device does not need CPython or a separately
installed MicroPython runtime.

## 6. Runtime ownership

`main()` is the composition root. It creates the long-lived objects and keeps
their lifetimes visible in one place.

This is intentional. Startup is not hidden inside an `IoTApplication` or
`RuntimeCoordinator` class. A reader can open `main.cpp` and see which objects
exist, the order in which they start, and the dependencies passed between
them. The small free functions in that file only select a display, print its
details, or record a stop signal; they do not own application components.

```text
main()
├── RuntimeConfig and RuntimePaths
├── DisplayManager
├── LinuxSystemInformationProvider
├── PythonApplicationLoader
├── ScreenManager
│   ├── JpegImageLoader
│   └── IRenderBackend (LVGL framebuffer implementation)
├── HttpFileDownloader
├── PythonApplicationManager
│   ├── MicroPythonApplicationContext    created per Python app
│   └── MicroPythonRuntime               created per Python app
└── ApplicationDeploymentService
    ├── ApplicationMessageQueue
    ├── MqttApplicationReceiver
    └── ApplicationDeploymentController
        ├── ApplicationDeploymentMessageParser
        └── TemporaryPythonApplicationInstaller
```

Most service objects are not copyable or movable. They own a thread, an open
device, an interpreter, or references to another long-lived service. Keeping
one owner avoids stale callbacks and double cleanup.

During normal shutdown, `main()` stops the deployment service, the Python
application manager, and then `ScreenManager`. Stopping the deployment service
stops its MQTT receiver. This is the reverse of the order in which those
running components were started. If startup throws an exception, their local
C++ objects are also destroyed in reverse construction order.

### 6.1 Lifetime groups

Process-long objects:

- Display discovery and system-information providers
- `ScreenManager` and the LVGL framebuffer backend
- `HttpFileDownloader` and its per-application download directory
- `ApplicationDeploymentService` and its MQTT receiver and message queue
- Application loader, installer, deployment controller, and application
  manager

Application-long objects:

- `MicroPythonApplicationContext`
- `MicroPythonRuntime`
- The MicroPython heap
- Python globals, callbacks, widgets IDs, and Python-created hardware objects
- Files downloaded by the current Python application

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
- Downloads files requested by Python and decodes JPEG cache misses.
- Changes the application state.
- Handles `SIGINT` and `SIGTERM` through a stop flag.

MicroPython never moves away from this thread.

### 7.2 Render thread

`ScreenManager::start()` creates the render thread and waits until the backend
has opened `/dev/fb0`. The thread then repeats this work:

1. Take up to 16 queued rendering commands in order.
2. Run those commands through the backend.
3. Call `lv_timer_handler()` so LVGL can update the display.
4. If commands are still waiting, start the next batch immediately.
5. Otherwise, wait for LVGL's requested delay, a new command, or shutdown.

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
| MQTT network thread | Main thread | The MQTT callback copies the received JSON text into the deployment service's `ApplicationMessageQueue`. The service wakes the main thread, removes one message, and gives it to `ApplicationDeploymentController`. If the queue is full, the new message is rejected. |
| Main thread | Render thread | A Python drawing request becomes a C++ drawing command. `ScreenManager::enqueueRenderCommand()` adds it to the render queue and wakes the render thread. The render thread handles commands in groups of up to 16 and lets LVGL refresh between groups. If the queue is full, the drawing request reports an error. |
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
4. Use that monitor's active mode from the same scan
5. Find and load the shipped default application
6. Start ScreenManager and open /dev/fb0 on the render thread
7. Create the Python application manager
8. Create a fresh MicroPython interpreter
9. Run the default application's main.py
10. Start `ApplicationDeploymentService`, which starts its MQTT receiver
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
ask ApplicationDeploymentService to wait until that deadline
       |
       +---- message arrived ----> process one deployment through the service
       |
       v
run Python callbacks that are now due
       |
       +----> repeat
```

The wait is never longer than one second. The deployment service waits on its
message queue, which wakes it early when MQTT receives a deployment. If several
messages are already queued, the next wait returns immediately. The main loop
does not need to know which deployment component receives, stores, or processes
the message.

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

`DisplayManager` scans DRM once during startup. Each returned monitor includes
the mode it was using at that time. `main()` chooses a monitor from this list
and builds `ActiveDisplay` from the monitor and mode in the same list. Startup
stops if the selected monitor does not have an active mode.

`main()` then moves the monitor list from this startup scan into
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

Python and the main loop call `ScreenManager` on the main thread. It accepts
values such as `TextBoxSpec` and puts drawing commands in the render queue.
Only access to that queue is protected by its mutex; widget IDs, image state,
and the JPEG cache belong to the main thread. Do not call its drawing methods
from MQTT callbacks or another worker thread.

Most drawing calls only queue work. An image call may first read and decode a
JPEG, so it waits until the pixels are ready before queueing the drawing work.

Its responsibilities are:

- Own the render thread and backend.
- Give each text box or normal image a process-wide widget ID.
- Decode JPEG files and reuse recently decoded pixels within a fixed memory
  limit.
- Keep render commands in order.
- Process commands in batches so LVGL can refresh while the queue is busy.
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
- Create, update, move, and delete normal JPEG images
- Set or remove a centred, fitted, or tiled background JPEG
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
5. `drawTextBox()` checks that width and height are positive, assigns a widget
   ID, and records it in a small set of text-box IDs. It creates a command
   that will later call `IRenderBackend::createTextBox()`.
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

The render loop handles no more than 16 commands before step 9. The number 16
is a maximum, not a minimum:

```text
10 commands -> process 10 -> refresh LVGL -> wait
20 commands -> process 16 -> refresh LVGL -> process 4 -> refresh LVGL -> wait
```

The second group starts without an extra sleep. This keeps drawing responsive
without changing the order of commands.

`draw_text_box()` returns the widget ID after the command has been queued. The
Python app keeps this ID and passes it to `update_text_box()`,
`move_text_box()`, or `delete_text_box()` when it wants to change the same text
box later.

`ScreenManager` checks this set before accepting a text update, move, or
deletion. It includes boxes whose creation is still queued, so Python can
create and update a box without waiting for a frame. Commands stay in order.
If queuing creation fails, its ID is removed from the set. A deletion removes
the ID only after its command has been queued successfully.

Clearing the screen, showing an emergency screen, or stopping the renderer
invalidates the text-box IDs. Application replacement clears the screen, so
the new application cannot use an old application's IDs. Width and height
are also checked before filled-area and emergency-screen requests are queued.

Invalid sizes or IDs raise an error on the main/Python thread. Python can
catch that error and continue. An unhandled error goes through the normal
emergency-screen handling; it does not reach the render thread. The LVGL
backend keeps its own checks for unexpected internal errors.

### 11.4 How a downloaded JPEG reaches the monitor

Downloading and drawing are separate operations. An application can download
a file once, keep the returned path, and use it in more than one drawing call.

`ScreenManager` owns the image-display work after Python supplies a file path.
It checks the request, asks its private `JpegImageLoader` to decode the JPEG,
keeps the bounded decoded-image cache, and sends the immutable pixels through
the render queue. When an application changes, `PythonApplicationManager` only
asks `ScreenManager` to clear the screen. `ScreenManager` then clears its image
state and cache itself.

The LVGL backend does not decode or cache images. It keeps a shared reference
to the decoded pixels while the related widget is on the screen. Deleting,
replacing, or clearing that widget releases the backend's reference.

```text
Python application
  network.download_file(url, expected_sha256=...)
          |
          v
MicroPython network module and C++ bridge
          |
          v
HttpFileDownloader -> libcurl -> current app's /tmp download directory
          |
          | returns a local path
          v
Python application
  display.set_background_image(path, mode="fit")
          |
          v
MicroPython display module and C++ bridge
          |
          v
ScreenManager -> JpegImageLoader -> libjpeg-turbo
          |
          | queues decoded pixels held by shared_ptr
          v
Render thread -> LVGL image widget -> /dev/fb0 -> HDMI monitor
```

The steps are:

1. `network.download_file()` checks the URL and optional SHA-256 text.
2. `HttpFileDownloader` writes the response to a private temporary file. It
   enforces the file, application-total, timeout, redirect, and protocol
   limits while libcurl is receiving the data.
3. The downloader calculates SHA-256, checks the expected value when one was
   supplied, and gives Python the completed file's local path.
4. Python passes that path to `draw_image()` or `set_background_image()`.
5. `JpegImageLoader` reuses matching decoded pixels when possible. Otherwise,
   libjpeg-turbo checks, scales, and decodes the JPEG on the main thread.
6. `ScreenManager` queues a render command that shares ownership of those
   pixels. The render thread creates or updates the LVGL image widget.
7. The LVGL backend keeps that shared owner for as long as the widget needs
   the pixel buffer.

The API is synchronous. Python waits during the download and a cache-miss
decode, while the separate render thread continues refreshing LVGL. There is
no decoder worker thread: starting one and immediately waiting for it would
add another queue and lifetime to manage without making the Python call
asynchronous.

Downloads and decoded pixels have separate limits. One file may use 10 MiB,
and all downloaded files stored by one application may use 50 MiB. The decoded
JPEG cache retains up to 32 MiB for reuse. It removes the least recently used
images only when no widget or queued command still needs them. Replacing an
image needs space for both the old and new pixels until the renderer applies
the change. If they cannot fit together, the call raises an error and leaves
the old image unchanged. Request a smaller scale to reduce this memory use.

Deleting a widget queues its removal; it does not immediately release its
pixels or remove its cache entry. Once the renderer deletes the widget, the
cache may evict those pixels when it needs room. A new application clears the
download directory and decoded cache. Old pixels remain valid until the render
thread finishes removing the old widgets.

The 32 MiB cache limit is not a limit on total process memory. Decoding needs
temporary compressed data, decoder working memory, and a new pixel buffer
before cache admission. Old widgets can also briefly hold pixels after a
screen clear. The LVGL framebuffer buffer and Python heap are separate.

Each network transfer has a 30-second limit, including up to 10 seconds to
connect. A timeout raises a Python `RuntimeError` and removes the partial file.
Python can catch that error; an unhandled error stops the application and shows
the C++ emergency screen. Failure to clear the previous application's download
directory is handled as a startup failure too. The default app is not restarted.

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

For an MQTT application, ApplicationDeploymentController owns the parser and
temporary installer. The parser checks the deployment first. The installer
then writes the checked metadata and source into a staging directory, renames
it to its final directory, and returns a PythonApplication using the same
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
| PythonApplicationManager | Owns one Python application session. It creates and destroys the context and interpreter, asks the runtime to process timers, tracks application state, and shows the C++ emergency screen after a failure. |
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
ApplicationDeploymentService
  |
  v
ApplicationDeploymentController
  |
  +--> validates the deployment message
  |
  +--> uses its TemporaryPythonApplicationInstaller
  |      to write the checked application under /tmp
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
Python callback is due. The manager passes that request to the runtime. The
runtime measures the elapsed time and reads the timer state from MicroPython:

```text
main loop
  |
  v
PythonApplicationManager
  |
  v
MicroPythonRuntime -> MicroPython scheduler
```

Python display, network, and system calls use a different path. Their C++
bridge files get the active context and use it to reach the required C++
service:

```text
Python application
  |
  v
Native MicroPython module
  |
  v
display_cpp_bridge.cpp, network_cpp_bridge.cpp, or system_cpp_bridge.cpp
  |
  v
MicroPythonApplicationContext
  |
  ├── ScreenManager
  ├── IFileDownloader
  ├── display information
  ├── startup system-information snapshot
  └── ISystemInformationProvider for live system reads
```

The input bridge does not use this context. Each Python gamepad object has its
own C++ gamepad handle, so its bridge functions can use that handle directly.

The display, network, and system bridges use one shared function to find the
active application context. The four C modules also share the function that
turns a failed native result into a Python `RuntimeError`. Each module still
parses its own arguments and keeps its own error messages.

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

`PythonApplicationManager` creates two objects because they do different jobs:

- `MicroPythonApplicationContext` gives the native bridge functions access to
  the C++ services used by the current application. It does not start or stop
  MicroPython.
- `MicroPythonRuntime` owns the interpreter and its fixed-size heap. It does
  not know about `ScreenManager`, downloads, or system information.

Their lifetime follows this order:

```text
No Python application is running
  active context = nullptr
          |
          v
Create MicroPythonApplicationContext
  native bridge functions can now reach C++ services
          |
          v
Create MicroPythonRuntime
  MicroPython starts and runs the application
          |
          v
Destroy MicroPythonRuntime
  MicroPython stops and releases its heap
          |
          v
Destroy MicroPythonApplicationContext
  active context = nullptr again
```

The context is created first so it is ready before Python can call a native
module. The runtime is destroyed first so the context remains available while
MicroPython shuts down. The active-context pointer must be nullable because no
context exists before an application starts, after it stops, or while one
application is being replaced by another. Only the main thread runs the
interpreter.

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
- File downloads for the current application
- The current application name

The process runs only one Python application at a time, so only one context can
be active.

### 13.5 Native module boundary

The public Python module is `iot`. It exposes five private native
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
├── mod_iot_network.c
├── mod_iot_scheduler.c
└── mod_iot_system.c
       |
       v
C-compatible bridge (`extern "C"` and `NativeBridgeErrorHandler`)
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
NativeBridgeErrorHandler::runSafely(...)   C++ exception boundary
  |
  v
ScreenManager::clear(...)
```

`NativeBridgeErrorHandler::runSafely()` is important because a C++ exception
must not continue into MicroPython's C code. It runs the C++ work inside `try`
and `catch` blocks and returns a plain C structure:

```text
C++ call succeeds
  -> { succeeded = 1, error_message = null }

C++ call throws an exception
  -> NativeBridgeErrorHandler catches it
  -> { succeeded = 0, error_message = "..." }
```

Display, input, network, and system calls use the same error helper. Each bridge
provides its own fallback text, such as `Unknown C++ gamepad error`, so an
unexpected exception still identifies the module that failed.

The helper copies the error into a 1 KiB fixed buffer in thread-local storage.
It shortens a longer message and adds `...` at the end. It does not create a
`std::string` while handling the exception, so it can also report
`std::bad_alloc` safely. The error pointer remains valid long enough for the C
binding to read it.

After the bridge has returned to C, the binding converts a failed result into a
MicroPython `RuntimeError`. This order means neither exception system crosses
into code that does not understand it.

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

Image creation follows the same bridge and queue. JPEG decoding happens before
the render command is queued, so the render thread receives ready pixel data.

### 14.2 `iot.network`

The network binding sends one HTTP or HTTPS request to `HttpFileDownloader`.
The downloader uses libcurl, writes into a bounded temporary file, and returns
the local path, SHA-256, size, content type, and whether it reused a cached
file. Supplying an expected SHA-256 is optional.

The call waits until the transfer finishes. This keeps the API and ownership
simple. The render thread remains independent, so the current screen still
refreshes while the main thread is waiting.

### 14.3 `iot.scheduler`

The scheduler stores repeating tasks in the MicroPython VM. Each task contains
a callback, positive ID, interval, and remaining time. MicroPythonRuntime owns
the clock used to update these remaining times.

When C++ asks for the next delay, the scheduler returns the shortest remaining
time. When that time has passed, it gathers the due callbacks and then runs
them one by one. Gathering first means a callback can add or cancel timers
without changing the list currently being walked.

If several intervals were missed, a repeating callback runs once rather than
being replayed many times in a burst. Its next deadline stays aligned with its
interval.

Time spent inside a callback counts towards the timer interval. For example,
if the next call is due in one second and the callback takes 300 milliseconds,
MicroPythonRuntime reports that about 700 milliseconds remain. Reading the
delay does not change the stored timer. The runtime updates it when the main
loop asks the runtime to process callbacks, so the same 300 milliseconds are
not counted twice.

### 14.4 `iot.system`

Most values come from a snapshot taken when the application starts. The
snapshot includes machine, resource, interface, and device details. This avoids
repeated full scans and gives those dashboard panels a consistent view.

Current local time, uptime, and network interfaces are live calls because those
values can change while a screen is running.

### 14.5 `iot.input`

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

`ApplicationDeploymentService` is the runtime's entry point to this subsystem.
It owns the MQTT receiver, the bounded message queue, and the deployment
controller. `main.cpp` starts and stops the service and asks it to process one
message during each main-loop pass.

The owned classes still have separate jobs. The receiver handles Mosquitto,
the queue carries data between threads, and the controller validates and
installs an application on the main thread.

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
   `accepted`.

`--dry-run` builds and validates the message without connecting. `--no-wait`
returns after broker acknowledgement instead of waiting for the device's final
result.

The sender waits up to 30 seconds for acceptance or a validation/installation
error. IoT App replies before compiling or executing the new Python app, so
the sender does not wait for its image downloads. Each device HTTP transfer
still has its own 30-second limit.

The previous app can still delay processing if it is busy on the main thread.
A reply timeout does not cancel the request or prove it failed; check the
device log before sending it again. Both IoT App and the sender must use this
acceptance-only protocol; older versions wait for a startup result instead.

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

These are transport checks only. The callback does not check JSON fields,
decode Python source, calculate its hash, write files, or call MicroPython.

The main thread calls
`ApplicationDeploymentService::waitForAndProcessOneMessage()`. The service
waits in `ApplicationMessageQueue::waitAndPopMessage()`. When a message is
available, it removes that message and passes it to
`ApplicationDeploymentController::process()`. The controller can then parse the
JSON, install the application, and start MicroPython.

`ApplicationDeploymentService` owns these deployment components, but it does
not merge their work. `MqttApplicationReceiver` still owns the libmosquitto
client, callbacks, connection, subscription, and publication work. The
controller uses the receiver's status-publishing interface instead of calling
libmosquitto directly.

The MQTT callback can arrive at any time. It puts the message in the queue and
returns without calling MicroPython. The main thread later removes the message
and does the parsing and MicroPython work on the thread that owns the
interpreter. The queue is only a short-lived thread boundary; installed
applications are kept in their temporary directories instead.

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
signature and does not prove who sent the application. Deployment validation
and file downloads use the same internal SHA-256 helper.

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
accepted
```

Possible final failure states are:

```text
rejected
failed
```

`accepted` means the package passed validation and its temporary files were
installed. The controller sends and remembers this reply before stopping the
current app or compiling the new Python source. The sender can then exit.

`rejected` means the message did not pass validation. `failed` means the
temporary installation could not complete. Neither changes the current app
or emergency screen.

Python syntax errors, startup exceptions, and scheduled-callback exceptions
are handled on the device. IoT App writes the traceback to its log and shows
the native emergency screen. It does not send another deployment result or
restart the default app.

MQTT QoS 1 may deliver the same install message more than once. The controller
remembers a bounded number of final results by transfer ID. A duplicate gets
the saved final answer instead of starting the same app again, even if that
app has since failed. This saved answer describes the original delivery, not
the app's current state. Sending again from the command line creates a new
transfer ID and starts a new attempt.

### 17.8 Reading the sender output

An accepted deployment produces output similar to this:

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
Device status: accepted: Application received and ready to execute
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
| `accepted` | The temporary files are installed. IoT App is about to stop the current app and compile and run the new source. This is the final successful delivery reply. |
| `rejected` | Message validation failed. The currently displayed app or emergency screen is left unchanged. |
| `failed` | Temporary installation failed. The currently displayed app or emergency screen is left unchanged. |

For example, if the device cannot write the temporary files, the reply ends
with `failed`. Its message describes the installation error:

```text
Device status: received: Message received by IoT App
Device status: validating: Source size and SHA-256 are valid
Device status: failed: Could not create temporary application file: /tmp/iot-app-<uid>/applications/.staging-<transfer-id>/main.py
```

The final MQTT reply for an accepted application is:

```json
{
  "transfer_id": "71b84271630a467aa16ee7b4a0c39632",
  "status": "accepted",
  "application_id": "moving-text-in-frame",
  "message": "Application received and ready to execute"
}
```

The sender reads this JSON and prints the shorter `Device status` line. It
exits with code `0` for `accepted` and code `2` for `rejected` or `failed`.
Connection errors and reply timeouts use exit code `1`.

The sender ignores a status message whose `transfer_id` does not match the
request it is waiting for.

#### Python errors after acceptance

The sender result is the same for all Python errors. Delivery has already
been accepted; Python execution is a separate step:

| When Python fails | What the Ubuntu sender reports | What the Raspberry Pi shows |
|---|---|---|
| While compiling the source, such as a syntax error | `accepted: Application received and ready to execute` | The native emergency screen shows the compilation error. No Python app remains running. |
| While running the entry point, such as `import os1` or a download timeout | `accepted: Application received and ready to execute` | The native emergency screen shows the startup traceback. No Python app remains running. |
| Later, inside a scheduled callback | `accepted: Application received and ready to execute` | IoT App stops Python and the native emergency screen shows the callback traceback. |

The current protocol does not send a second status for compilation, startup,
or callback errors. The sender can report success while the device shows an
error: success means delivery, not successful execution. Check the screen or
device log to see whether Python is running correctly.

IoT App writes the full traceback to the Raspberry Pi log; it is not included
in the MQTT reply. The emergency screen stays visible until another external
application starts or `iot_app` restarts. The default app is not restored after
a Python failure.

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
| Current local time | `time()`, `localtime_r()`, and `strftime()` |
| Uptime and memory | `sysinfo()` and `/proc/meminfo` |
| Root storage | `statvfs("/")` |
| Network interfaces and IPv4 | `getifaddrs()` |
| Link speed | `/sys/class/net/<name>/speed` |
| Interface and device counts | `/dev` and `/sys/class` scans |

Missing optional values do not fail the complete dashboard. Text values use
`Unavailable`; optional numeric values become Python `None`.

The system bridge also uses this provider for live values. It asks the
provider for the current local time, uptime, and network interfaces instead of
calling Linux directly.

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

Startup also creates one `RuntimePaths` value for temporary files:

```text
RuntimePaths
└── /tmp/iot-app-<user-id>/
    ├── downloads/
    └── applications/
```

`runtime_config.cpp` calculates these paths once. `main.cpp` gives the
downloads directory to `HttpFileDownloader` and the applications directory to
`ApplicationDeploymentService`. Both therefore use the same per-user root
without rebuilding directory names in different parts of startup.

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
| One downloaded file | 10 MiB |
| Downloaded files stored by one Python application | 50 MiB |
| HTTP/HTTPS redirects | 5 |
| HTTP/HTTPS connection and total timeout | 10 and 30 seconds |
| Original JPEG dimensions | 8192 pixels per side and 16,777,216 pixels total |
| One decoded JPEG | 32 MiB |
| Decoded JPEG cache | 32 MiB; images still used by widgets or queued commands are not evicted |

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
| A download or JPEG check fails | Raise `RuntimeError` in the current Python application |
| MQTT message queue is full | Drop the new message and log it |
| Render command queue is full | Raise an error to the calling application |
| Render thread fails | Main loop rethrows and stops the process |
| Deployment message is invalid | Publish `rejected` when a safe transfer ID exists |
| External app startup fails | Stop Python and show native emergency screen |
| External callback fails | Stop Python and show native emergency screen |
| Default callback fails | Stop Python and show native emergency screen |

An invalid MQTT message without a safe transfer ID cannot be matched to a safe
transfer-specific status topic. It is logged and dropped.

## 23. Linux image integration

IoT App can run on Raspberry Pi OS or in the project's Buildroot and Yocto
images. These systems use the same C++ runtime and default Python application,
but install packages and services in different ways.

Image creation and device administration are kept outside this architecture
guide:

- [Raspberry Pi OS](../raspberry-pi-os/README.md)
- [Buildroot image](../buildroot/README.md)
- [Yocto image](../yocto/README.md)
- [Shared device setup and troubleshooting](../device-image/README.md)
- [Shared image-support files](../../image_support/README.md)

## 24. Source layout by responsibility

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
  deployment service, MQTT receiving, parsing, queueing, temporary
  installation, status, and activation

micropython_iot_modules/
  thin MicroPython C bindings and C-to-C++ bridges

default_python_application/
  shipped default dashboard

buildroot_external/
  final image package, service, users, permissions, and board configuration
```


## 25. Important system rules

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
