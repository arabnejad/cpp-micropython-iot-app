# MicroPython API guide

Python applications running inside IoT App can import the project-owned
`iot` module:

```python
from iot import display, input, scheduler, system
```

These modules are built into the IoT App executable. They are not part of
standard MicroPython or CPython, and they are not available when the same
script is run with a normal `python3` command.

The [system design document](../system-design/README.md) explains the C++
ownership, threading, deployment, and failure handling behind these modules.

The public modules are:

| Module | What it provides |
|---|---|
| `iot.display` | Screen size, monitor details, text boxes, and filled areas |
| `iot.input` | The Adafruit Mini I2C STEMMA QT Gamepad driver |
| `iot.scheduler` | Repeating callbacks for live applications |
| `iot.system` | Linux, resource, network, device, and runtime information |

Names beginning with `_iot_` are private implementation modules. Applications
should always import through `iot` as shown above.

Complete applications using these APIs are available in
[`iot_app_sender/sample_applications`](../../../iot_app_sender/sample_applications/README.md).

## Application lifecycle

An application's `main.py` creates its screen and then returns. Returning does
not stop the application. IoT App keeps the MicroPython interpreter alive and
runs callbacks registered with `scheduler.every()`.

Do not put a permanent loop in `main.py`, and do not create a background thread
for screen updates. A long-running loop would prevent IoT App from processing
scheduled work or switching to a newly received application.

All positions and sizes in the display API are measured in pixels. RGB colour
components use integers from `0` to `255`.

## `iot.display`

The display module draws through the screen service owned by IoT App. Drawing
commands are sent to the LVGL render thread; Python code does not call LVGL
directly.

### `display.clear()`

```python
from iot import display

display.clear(color=(0, 0, 0))
```

Removes everything drawn by the application and fills the screen with one
colour. It returns `None`.

| Parameter | Required? | Meaning |
|---|---|---|
| `color` | No, default `(0, 0, 0)` | `(red, green, blue)` background colour; each value is from 0 to 255 |

The colour arguments are keyword-only.

### `display.draw_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="IoT App is running",
    text_color=(255, 255, 255),
    background_color=(0, 0, 0),
    border_color=(255, 255, 255),
    background_opacity=0,
    border_width=0,
    font_size=24,
)
```

Creates a text box and returns its positive integer widget ID. Keep this ID if
the box will be updated, moved, or deleted later.

The defaults shown above come from the C++ `TextBoxSpec`. The MicroPython
binding only replaces a style when the application supplies that argument.

| Parameter | Required? | Meaning |
|---|---|---|
| `x` | Yes | Horizontal position of the left edge |
| `y` | Yes | Vertical position of the top edge |
| `width` | Yes | Width of the text box; use a positive value |
| `height` | Yes | Height of the text box; use a positive value |
| `text` | Yes | Text shown in the box |
| `text_color` | No | `(red, green, blue)` text colour; each value is from 0 to 255 |
| `background_color` | No | `(red, green, blue)` box colour; each value is from 0 to 255 |
| `border_color` | No | `(red, green, blue)` border colour; each value is from 0 to 255 |
| `background_opacity` | No | `0` is transparent and `255` is fully solid |
| `border_width` | No | Border thickness in pixels; `0` hides the border |
| `font_size` | No | Requested font size in pixels; must be from 1 to 65535 |

The style arguments after `text` are keyword-only. Position and size values
must fit in a signed 32-bit integer. `border_width` and `font_size` may not be
larger than 65535.

Only fonts compiled into the C++ application are available. The current build
contains Montserrat at 14, 20, 24, and 32 pixels. The requested size selects
one of those fonts:

| Requested `font_size` | Font used |
|---|---|
| 1 to 14 | Montserrat 14 |
| 15 to 20 | Montserrat 20 |
| 21 to 24 | Montserrat 24 |
| 25 to 65535 | Montserrat 32 |

The API does not currently accept a font-family name.

### `display.update_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="Starting",
)

display.update_text_box(widget_id, "IoT App is ready")
```

Changes the text in an existing text box and returns `None`.

| Parameter | Required? | Meaning |
|---|---|---|
| `widget_id` | Yes | Positive ID returned by `draw_text_box()` |
| `text` | Yes | Replacement text |

### `display.move_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="Move me",
)

display.move_text_box(widget_id, 200, 160)
```

Moves an existing text box to a new absolute position and returns `None`. Its
size and text do not change.

| Parameter | Required? | Meaning |
|---|---|---|
| `widget_id` | Yes | Positive ID returned by `draw_text_box()` |
| `x` | Yes | New horizontal position of the left edge |
| `y` | Yes | New vertical position of the top edge |

### `display.delete_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="Delete me",
)

display.delete_text_box(widget_id)
```

Deletes one existing text box and returns `None`. The widget ID must not be
used again after the box has been deleted.

| Parameter | Required? | Meaning |
|---|---|---|
| `widget_id` | Yes | Positive ID returned by `draw_text_box()` |

### `display.fill_area()`

```python
from iot import display

display.fill_area(
    x=40,
    y=40,
    width=300,
    height=150,
    color=(0, 0, 0),
)
```

Draws a solid rectangle and returns `None`. A filled area does not currently
have an ID and cannot be moved, updated, or deleted separately. Use
`display.clear()` to remove it.

| Parameter | Required? | Meaning |
|---|---|---|
| `x` | Yes | Horizontal position of the left edge |
| `y` | Yes | Vertical position of the top edge |
| `width` | Yes | Rectangle width; use a positive value |
| `height` | Yes | Rectangle height; use a positive value |
| `color` | No, default `(0, 0, 0)` | `(red, green, blue)` fill colour; each value is from 0 to 255 |

The colour arguments are keyword-only.

### `display.size()`

```python
from iot import display

width, height = display.size()
```

Takes no arguments and returns the active display size as a `(width, height)`
tuple. IoT App uses the Linux display mode that was already active; this
function does not change the resolution.

### `display.monitors()`

```python
from iot import display

monitors = display.monitors()
print("Connected monitors:", len(monitors))
```

Takes no arguments and returns a list containing every monitor found when IoT
App started. For example:

```python
[
    {
        "connector_name": "HDMI-A-1",
        "manufacturer": "AOC",
        "model": "U27B3A",
        "serial_number": "123456",
        "physical_width_mm": 600,
        "physical_height_mm": 340,
        "active": True,
        "current_mode": {
            "name": "1920x1080",
            "width": 1920,
            "height": 1080,
            "refresh_rate_hz": 60,
            "preferred": True,
            "interlaced": False,
        },
        "supported_modes": [
            {
                "name": "1920x1080",
                "width": 1920,
                "height": 1080,
                "refresh_rate_hz": 60,
                "preferred": True,
                "interlaced": False,
            },
        ],
    },
]
```

`active` identifies the monitor used by IoT App. `current_mode` is `None` when
the monitor is connected but does not have an active DRM mode. A preferred mode
is the mode recommended by the monitor. An interlaced mode draws alternating
sets of lines rather than a complete frame at once.

The manufacturer, model, serial number, and mode name may be empty strings when
Linux or the monitor does not provide them. Physical width and height may be
zero for the same reason.

The list is a startup snapshot. Connecting or disconnecting a monitor later
does not update it. Restart IoT App to scan the displays again.

### `display.active_monitor()`

```python
from iot import display

monitor = display.active_monitor()
current_mode = monitor["current_mode"]

print(monitor["connector_name"])
print(current_mode["width"], current_mode["height"])
```

Takes no arguments and returns the monitor dictionary marked as active in
`display.monitors()`. IoT App always has an active monitor while a Python
application is running, so `current_mode` is available in this result.

### Display example

```python
from iot import display

screen_width, screen_height = display.size()
display.clear(color=(8, 13, 22))

message_box = display.draw_text_box(
    x=40,
    y=40,
    width=screen_width - 80,
    height=100,
    text="IoT App is running",
    background_color=(24, 34, 51),
    border_color=(64, 220, 255),
    background_opacity=255,
    border_width=2,
    font_size=24,
)

display.update_text_box(message_box, "Display is ready")
display.move_text_box(message_box, 40, 160)
```

## `iot.scheduler`

The scheduler keeps an application responsive without an infinite Python loop.
Callbacks run one at a time on the MicroPython owner thread.

### `scheduler.every()`

```python
from iot import scheduler

def update_display():
    print("Scheduled callback ran")

task_id = scheduler.every(
    milliseconds=1000,
    callback=update_display,
)
```

Registers a repeating callback and returns a positive integer task ID. Both
arguments are required and may be passed by position or by name.

| Parameter | Required? | Meaning |
|---|---|---|
| `milliseconds` | Yes | Time between calls, from 1 millisecond to about 49.7 days |
| `callback` | Yes | Callable that accepts no arguments |

The callback is not called immediately. For example, a 1,000 millisecond timer
first runs after about one second, then runs again about once per second.

One application can have up to 128 active timers. This is a fixed safety limit,
not a Raspberry Pi hardware limit. It prevents a faulty application from
creating timers forever and using all available memory. Most applications only
need a few timers.

The scheduler stores the interval as an unsigned 32-bit millisecond value. Its
valid range is 1 to 4,294,967,295 milliseconds, or about 49.7 days.

If IoT App is busy and misses several calls, it calls the callback only once
when it becomes available again. It does not rapidly repeat the callback to
catch up. For example, if 3.4 seconds pass before IoT App can check a
one-second timer, the callback runs once and its next call is due about 0.6
seconds later.

Callbacks should finish quickly. They may add or cancel timers, but they should
not sleep for a long time or run forever.

### `scheduler.cancel()`

```python
from iot import scheduler

def update_display():
    print("Scheduled callback ran")

task_id = scheduler.every(
    milliseconds=1000,
    callback=update_display,
)

was_cancelled = scheduler.cancel(task_id)
```

Cancels one timer. It returns `True` when the task existed and was removed, or
`False` when the ID was invalid or no active task had that ID.

| Parameter | Required? | Meaning |
|---|---|---|
| `task_id` | Yes | ID returned by `scheduler.every()` |

### `scheduler.clear()`

```python
from iot import scheduler

scheduler.clear()
```

Takes no arguments, removes every scheduled task in the current application,
and returns `None`.

IoT App also removes all tasks automatically when it stops the current Python
application. Timer IDs belong only to the interpreter that created them.

### Scheduler example

```python
from iot import display, scheduler, system

clock_box = display.draw_text_box(
    x=40,
    y=40,
    width=420,
    height=70,
    text=system.current_time(),
)

def update_clock():
    display.update_text_box(clock_box, system.current_time())

clock_task = scheduler.every(
    milliseconds=1000,
    callback=update_clock,
)

# This can be called later when the clock no longer needs updates:
# scheduler.cancel(clock_task)
```

If a scheduled callback raises an unhandled exception, IoT App stops that
application and shows the traceback on the native emergency screen. The
shipped default application is not started again until `iot_app` restarts.

## `iot.system`

Most system functions use a snapshot taken when the current Python application
started. Related values therefore come from the same point in time, and the
runtime does not keep scanning `/proc`, `/sys`, and `/dev`.

`current_time()`, `uptime_seconds()`, and `network_interfaces()` are live reads.
Call them when a screen needs to show changing time, uptime, or network state.

### `system.information()`

```python
from iot import system

information = system.information()
```

Takes no arguments and returns:

```python
{
    "hostname": "raspberrypi",
    "device_model": "Raspberry Pi 4 Model B Rev 1.4",
    "operating_system": "Raspberry Pi OS",
    "kernel_version": "Linux 6.12.47-v8+",
    "architecture": "aarch64",
    "uptime_seconds": 6138,
}
```

The `uptime_seconds` value in this dictionary is the startup snapshot. Use
`system.uptime_seconds()` for a value that changes while the app is running.
The `kernel_version` value already starts with `Linux`, so applications do not
need to add that word themselves.

### `system.current_time()`

```python
from iot import system

local_time = system.current_time()
```

Takes no arguments and returns the current Linux local time as a string in
`YYYY-MM-DD HH:MM:SS` format, for example `2000-01-01 00:00:00`.

### `system.uptime_seconds()`

```python
from iot import system

uptime = system.uptime_seconds()
```

Takes no arguments and performs a live read of Linux uptime. It returns the
number of seconds since the system booted.

### `system.resources()`

```python
from iot import system

resources = system.resources()
```

Takes no arguments and returns the resource snapshot:

```python
{
    "logical_cpu_count": 4,
    "cpu_temperature_celsius": 43.2,
    "one_minute_load_average": 0.18,
    "total_memory_bytes": 2000000000,
    "available_memory_bytes": 1500000000,
    "root_storage_total_bytes": 16000000000,
    "root_storage_available_bytes": 12000000000,
}
```

`cpu_temperature_celsius` and `one_minute_load_average` are `None` when Linux
does not provide those values. Byte counts are integers.

### `system.network_interfaces()`

```python
from iot import system

network_interfaces = system.network_interfaces()
```

Takes no arguments, reads Linux at the time of the call, and returns a tuple
containing interfaces that can connect to another device, such as `eth0` and
`wlan0`. The local-only `lo` interface is not included:

```python
(
    {
        "name": "eth0",
        "connected": True,
        "ipv4_address": "192.0.2.10",
        "speed_megabits_per_second": 1000,
    },
)
```

`ipv4_address` is an empty string when no IPv4 address was found. Link speed is
`None` when Linux does not report it. Call this function again to detect a
later connection, disconnection, or address change.

### `system.interfaces()`

```python
from iot import system

system_interface_counts = system.interfaces()
```

Takes no arguments and returns counts of Linux hardware interfaces:

```python
{
    "i2c": 1,
    "gpio_controllers": 2,
    "spi": 0,
    "serial": 2,
}
```

The I2C value counts interfaces such as `/dev/i2c-1`; it does not scan I2C
addresses and does not report how many I2C devices are attached.

These values count Linux interfaces, not connected peripheral devices:

- `i2c` counts device files named `/dev/i2c-*`. For example, `/dev/i2c-1`
  counts as one I2C interface. An I2C address is only needed when communicating
  with a device on that interface.
- `gpio_controllers` counts `/dev/gpiochip*` device files. For example,
  `/dev/gpiochip0` and `/dev/gpiochip1` count as two GPIO controllers. This
  does not count individual GPIO inputs or outputs.
- `spi` counts Linux `spidev` interfaces.
- `serial` counts supported Linux serial device files, including `ttyAMA`,
  `ttyUSB`, `ttyACM`, and `ttyS` devices.

Therefore, `"i2c": 1` means Linux exposes one I2C bus device file. It does not
mean that one I2C peripheral was found or that the application has permission
to open the bus.

### `system.devices()`

```python
from iot import system

connected_device_counts = system.devices()
```

Takes no arguments and returns device counts from the startup snapshot:

```python
{
    "usb": 4,
    "input": 3,
    "block": 2,
}
```

### `system.app_information()`

```python
from iot import system

application_information = system.app_information()
```

Takes no arguments and returns information about the running IoT App process
and current Python application:

```python
{
    "application_name": "Default app",
    "app_version": "0.1.0",
    "micropython_version": "1.28.0",
    "lvgl_version": "9.5.0",
}
```

`application_name` comes from the current package's `app.json`. `app_version`
is the version of the C++ IoT App executable; it is not read from `app.json`.

### System example

```python
from iot import display, system

machine = system.information()
resources = system.resources()

summary = (
    "Host: %s\n"
    "Model: %s\n"
    "Kernel: %s %s\n"
    "CPU cores: %d\n"
    "Uptime: %d seconds"
) % (
    machine["hostname"],
    machine["device_model"],
    machine["kernel_version"],
    machine["architecture"],
    resources["logical_cpu_count"],
    system.uptime_seconds(),
)

display.clear()
display.draw_text_box(
    x=40,
    y=40,
    width=900,
    height=300,
    text=summary,
)
```

## `iot.input`

The input module currently provides one hardware driver:
`input.AdafruitMiniI2cGamepad`. It supports the Adafruit Mini I2C STEMMA QT
Gamepad with seesaw.

`GamepadJoystick` and `GamepadButtons` are view types returned by a gamepad.
Applications should not try to construct these view types directly.

### `input.AdafruitMiniI2cGamepad()`

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
```

Creates the Python and C++ gamepad objects. The constructor immediately opens
the selected Linux I2C device, such as `/dev/i2c-1`, and selects the requested
address. The values must be within the ranges below. A missing bus or a
permission problem raises `RuntimeError` here.

The constructor only opens the Linux I²C device. The application must then call
`connect()` to reset the gamepad, verify its product ID, configure its inputs,
and read its first state.

| Parameter | Required? | Meaning |
|---|---|---|
| `i2c_bus_number` | Yes | Linux I2C bus number from 0 to 255; `1` uses `/dev/i2c-1` |
| `i2c_address` | Yes | Normal seven-bit device address from `0x03` to `0x77`; this gamepad normally uses `0x50` |

These arguments have no defaults. The application must
state which Linux bus and address its hardware uses.

```python
from iot import input

# Construction opens /dev/i2c-1 and selects address 0x50.
gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)

# Connection now communicates with the gamepad and prepares it for use.
gamepad.connect()

print(gamepad.is_connected())  # True
```

### Gamepad methods

#### `gamepad.connect()`

Communicates with the selected I2C device, resets its processor, checks that it
reports the expected product ID, configures its button inputs, and reads an
initial state. It takes no arguments and returns `None`. A missing gamepad,
wrong device, or I2C transfer problem raises `RuntimeError`.

#### `gamepad.calibrate_joystick()`

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()

gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)
```

Measures the resting joystick centre. Keep the stick untouched while this runs.
Both arguments are optional and keyword-only.

| Parameter | Required? | Meaning |
|---|---|---|
| `number_of_samples` | No, default `20` | Number of joystick readings used to calculate the resting centre; must be greater than zero |
| `dead_zone` | No, default `100` | Distance the joystick must move away from its measured centre before `direction()` reports a direction; from 0 to 1023 |

Call `connect()` first.

`number_of_samples` controls how the centre is measured. The driver reads both
joystick axes several times and uses their average as the centre. More samples
can reduce small changes caused by electrical noise, but calibration takes
longer. Keep the joystick still and near its natural centre until calibration
finishes. The default of 20 is normally enough.

`dead_zone` prevents tiny movements around the centre from being treated as a
direction. For example, if the measured X centre is 510 and the dead zone is
100, X values from 410 through 610 are treated as centred. A value above 610
can report right, and a value below 410 can report left. The same rule is used
for the Y axis. The dead zone only affects `direction()`; it does not change
the values returned by `position()`.

#### `gamepad.refresh_input_state()`

Reads the current joystick and buttons from the physical device and remembers
the result. It takes no arguments and returns `None`.

Call this once at the start of each input update. After that, read everything
you need from `joystick` and `gamepad_buttons`. All those values then belong to
the same refresh cycle:

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)

joystick = gamepad.joystick()
gamepad_buttons = gamepad.buttons()

gamepad.refresh_input_state()

current_direction = joystick.direction()
pressed_buttons = gamepad_buttons.pressed()
```

`gamepad.joystick()` and `gamepad.buttons()` create view objects for reading the
state stored by `gamepad`. Create these views once and reuse them during later
input updates.

Methods such as `joystick.direction()` and `gamepad_buttons.pressed()` only
return the state remembered by the most recent `refresh_input_state()` call.
They do not contact the I2C gamepad again. Call `refresh_input_state()` later
when you want new values from the hardware.

`current_direction` is only needed when the application uses the joystick. For
example, this application moves a text box with the joystick:

```python
from iot import display, input, scheduler

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)

joystick = gamepad.joystick()

box_x = 200
box_y = 200
text_box = display.draw_text_box(
    x=box_x,
    y=box_y,
    width=300,
    height=80,
    text="Move me with the joystick",
)

def move_text_box_with_joystick():
    global box_x
    global box_y

    gamepad.refresh_input_state()
    current_direction = joystick.direction()

    if current_direction in ("left", "up_left", "down_left"):
        box_x -= 10
    elif current_direction in ("right", "up_right", "down_right"):
        box_x += 10

    if current_direction in ("up", "up_left", "up_right"):
        box_y -= 10
    elif current_direction in ("down", "down_left", "down_right"):
        box_y += 10

    if current_direction != "center":
        display.move_text_box(text_box, box_x, box_y)

scheduler.every(milliseconds=50, callback=move_text_box_with_joystick)
```

`joystick.direction()` applies the calibrated centre and dead zone. The
application does not need to compare the raw X and Y values itself.

#### `gamepad.is_connected()`

Takes no arguments and returns `True` after `connect()` succeeds. It reports
the driver's state and does not send a new probe transaction.

#### `gamepad.model_name()`

Takes no arguments and returns `"Adafruit Mini I2C STEMMA QT Gamepad"`.

#### `gamepad.joystick()`

Takes no arguments and returns a `GamepadJoystick` view connected to this
gamepad. Keep the gamepad open while using the view.

#### `gamepad.buttons()`

Takes no arguments and returns a `GamepadButtons` view connected to this
gamepad. Keep the gamepad open while using the view.

#### Diagnostic methods

These methods take no arguments and return integer values read during
`connect()`:

| Method | Meaning |
|---|---|
| `processor_hardware_id()` | Hardware ID reported by the gamepad processor |
| `firmware_product_id()` | Product ID from the upper 16 bits of the combined value; expected value is 5743 |
| `firmware_date_code()` | Encoded firmware date from the lower 16 bits |
| `combined_product_id_and_firmware_date_code()` | Original 32-bit product/date value reported by the device |

Call `connect()` before reading diagnostics. Until a successful connection,
these methods return their initial zero values because no identity registers
have been read yet.

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()

print("Processor hardware ID:", gamepad.processor_hardware_id())
print("Product ID:", gamepad.firmware_product_id())
print("Firmware date code:", gamepad.firmware_date_code())
print(
    "Combined product and date value:",
    gamepad.combined_product_id_and_firmware_date_code(),
)
```

`connect()` already checks that the product ID is 5743. These methods are
mainly useful for logs and hardware diagnostics.

#### `gamepad.close()`

Releases the native gamepad and closes its I2C connection. It takes no
arguments, returns `None`, and is safe to call more than once. Any later method
call on the gamepad or one of its views raises `ValueError`. MicroPython also
releases the object during garbage collection, but explicit `close()` is useful
when the device is no longer needed.

### `GamepadJoystick` methods

| Method | Return value |
|---|---|
| `position()` | Returns the latest `(x, y)` values remembered by `refresh_input_state()`. Each value is normally from 0 to 1023. X increases towards the right and Y increases upwards. |
| `centre()` | Returns the `(x, y)` resting centre measured by the latest `calibrate_joystick()` call. It does not change during normal input refreshes. |
| `dead_zone()` | Returns the dead-zone value supplied during calibration. Movement inside this distance from the centre is ignored by `direction()`. |
| `direction()` | Compares the latest position with the centre and dead zone, then returns a simple direction name such as `"left"`, `"up_right"`, or `"center"`. |

`direction()` returns one of:

```text
center, left, right, up, down,
up_left, up_right, down_left, down_right
```

Larger X values mean right and larger Y values mean up.

### `GamepadButtons` methods

#### `gamepad_buttons.pressed()`

Takes no arguments and returns a tuple containing every button that is held
down. An empty tuple means no buttons are pressed. Names are returned in this
order:

```text
X, Y, A, B, Select, Start
```

Use `pressed()` when you need the complete button state, for example:

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()

gamepad_buttons = gamepad.buttons()

gamepad.refresh_input_state()
print(gamepad_buttons.pressed())  # For example: ("A", "Start")
```

#### `gamepad_buttons.is_pressed()`

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()

gamepad_buttons = gamepad.buttons()
gamepad.refresh_input_state()

is_down = gamepad_buttons.is_pressed("A")
if is_down:
    print("A is held down")
```

Returns `True` when one named button is held down.

| Parameter | Required? | Meaning |
|---|---|---|
| `button_name` | Yes | One of `"X"`, `"Y"`, `"A"`, `"B"`, `"Select"`, or `"Start"` |

Button names are case-sensitive. An unknown name raises `ValueError`.

Use `is_pressed()` when you only care about one button, as shown by the
`is_down` condition above.

Both methods use the state saved by the latest `refresh_input_state()` call.
They report whether a button is currently held down; they do not report a
separate one-time "button was just pressed" event.

### Input example

```python
from iot import display, input, scheduler

gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=1,
    i2c_address=0x50,
)
gamepad.connect()

# Do not touch the joystick while its resting centre is measured.
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)

joystick = gamepad.joystick()
gamepad_buttons = gamepad.buttons()

status_box = display.draw_text_box(
    x=40,
    y=40,
    width=800,
    height=180,
    text="Waiting for gamepad input",
)

def refresh_gamepad():
    gamepad.refresh_input_state()
    pressed_buttons = gamepad_buttons.pressed()
    pressed_buttons_text = ", ".join(pressed_buttons) if pressed_buttons else "none"
    display.update_text_box(
        status_box,
        "Direction: %s\nButtons: %s" % (
            joystick.direction(),
            pressed_buttons_text,
        ),
    )

scheduler.every(milliseconds=50, callback=refresh_gamepad)
```

## Errors

Invalid Python arguments normally raise `TypeError` or `ValueError`. Errors
reported by the C++ display, Linux, or hardware layers raise `RuntimeError`.

An unhandled startup or scheduled-callback exception stops the current Python
application. IoT App displays the traceback on its native emergency screen and
writes it to the terminal or service log. No Python application remains
running. The emergency screen stays visible until another valid external
application arrives or `iot_app` restarts.
