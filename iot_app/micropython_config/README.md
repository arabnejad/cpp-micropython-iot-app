# Embedded MicroPython configuration

This directory controls the MicroPython interpreter compiled into IoT App. It
does not contain Python applications. Its files decide which MicroPython
features are available and tell the MicroPython build where to find the native
modules provided by this project.

MicroPython normally calls an importable item a module. Python packages are
directories containing several modules. This project currently supports
built-in modules, but it does not load Python packages from the filesystem.

## Files in this directory

### `mpconfigport.h`

This header contains compile-time MicroPython settings. For example:

```c
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_IO (0)
```

`1` normally enables a feature and `0` disables it. These settings change the
interpreter executable, so IoT App must be rebuilt after changing them.

This file is the starting point for enabling an optional module that already
exists in the pinned MicroPython source tree. A setting can include or exclude
code only when that source code is already part of the build. Some MicroPython
extension modules also need source-build changes and operating-system support.

It is not the place to implement a new IoT App module.

### `micropython_embed.mk`

This small Make file starts MicroPython's embed build. It supplies three paths:

- `MICROPYTHON_TOP` points to the pinned MicroPython submodule.
- `PACKAGE_DIR` is where generated embed files are written.
- `USER_C_MODULES` points to `iot_app/micropython_iot_modules`.

The last line includes MicroPython's `ports/embed/embed.mk`, which performs the
actual generation work.

The build follows this path:

```text
CMake builds IoT App
        |
        +--> runs micropython_config/micropython_embed.mk
        |         |
        |         +--> reads mpconfigport.h
        |         |
        |         +--> scans each native module listed by micropython.mk
        |         |
        |         v
        |     generates MicroPython core source and module tables
        |
        +--> compiles the generated core source
        |
        +--> compiles the project module sources listed in CMakeLists.txt
        |
        v
links everything into iot_app
```

Generated files belong in the build directory. Do not edit them because the
next generation step will replace them.

## Enabling a module supplied by MicroPython

First find the option controlling the module in the pinned MicroPython source.
For example:

```bash
rg "MICROPY_PY_TIME" micropython/py/mpconfig.h micropython/extmod
```

Then check four things:

1. Which setting enables the module.
2. Whether its source file is already included by the embed build.
3. Whether that module needs other settings.
4. Whether the embed port must provide operating-system functions for it.

Add the required setting to `mpconfigport.h`. If the checks above found missing
source files or platform functions, add those too. Then rebuild IoT App and
test the module from a Python application.

### Example: `sys`

The `sys` module is already enabled:

```c
#define MICROPY_PY_SYS (1)
```

An application can therefore use it directly:

```python
import sys

print(sys.platform)
```

`sys.platform` returns `iot-app` because `mpconfigport.h` also defines
`MICROPY_PY_SYS_PLATFORM`.

### F-strings

IoT App enables formatted string literals:

```c
#define MICROPY_PY_FSTRINGS (1)
```

Applications can therefore place values directly inside readable strings:

```python
temperature_celsius = 42.5
message = f"Temperature: {temperature_celsius} C"
```

### Detailed error messages

IoT App also enables MicroPython's detailed error-reporting level:

```c
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_DETAILED)
```

Python exceptions can therefore include more context, such as the relevant
object, type, or function name. IoT App records this information in its log and
uses it when it displays an application failure on the emergency screen.

### Example: `time`

MicroPython provides a `time` module, but the core feature level selected by
IoT App does not enable it. The first setting would be:

```c
#define MICROPY_PY_TIME (1)
```

Adding this line alone is not enough in this project for two reasons.

First, `time` is implemented by MicroPython's `extmod/modtime.c`. The minimal
embed package currently copies MicroPython's core `py` sources, but it does not
copy and compile `modtime.c`. Supporting it therefore requires updating
`iot_app/cmake/micropython.cmake` and the `iot_runtime` source list, along with
any other upstream files used by `modtime.c`.

Second, functions such as `time.sleep()` and `time.ticks_ms()` call timing
functions that the platform must implement, including:

```text
mp_hal_delay_ms()
mp_hal_delay_us()
mp_hal_ticks_ms()
mp_hal_ticks_us()
mp_hal_ticks_cpu()
```

The optional calendar and clock functions need more settings and Linux
implementations:

```c
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS (1)
```

Do not enable these options until the source files and required platform
functions have been added and tested. Otherwise, the build can fail because a
source file is missing or because MicroPython refers to a platform function
that IoT App does not provide.

IoT App already provides these simpler project APIs:

```python
from iot import scheduler, system

print(system.current_time())
```

Use `iot.system` and `iot.scheduler` unless compatibility with MicroPython's
standard `time` API is specifically needed.

## Adding a new IoT App native module

A project-owned native module belongs under `micropython_iot_modules`, not in
`mpconfigport.h` and not inside the MicroPython submodule.

For example, a new sensor module could use this layout:

```text
iot_app/micropython_iot_modules/sensor/
├── micropython.mk
├── mod_iot_sensor.c
├── sensor_cpp_bridge.cpp
└── sensor_cpp_bridge.h
```

The C file converts between Python values and a small C-compatible bridge. The
C++ bridge then calls the normal IoT App classes. This is the same structure
used by the existing display, input, network, scheduler, and system modules.

### 1. Add the module to MicroPython's generation step

Create `sensor/micropython.mk`:

```makefile
IOT_SENSOR_MODULE_DIR := $(USERMOD_DIR)

SRC_USERMOD_C += $(IOT_SENSOR_MODULE_DIR)/mod_iot_sensor.c
CFLAGS_USERMOD += -I$(IOT_SENSOR_MODULE_DIR) \
                 -I$(IOT_SENSOR_MODULE_DIR)/../common
```

The IoT App build already passes `micropython_iot_modules` as
`USER_C_MODULES`. MicroPython looks in each immediate child directory for a
file named `micropython.mk`.

During generation, MicroPython scans the listed C source for names such as
`MP_QSTR_sensor` and for `MP_REGISTER_MODULE`. It uses that information to
generate the string and module-definition headers needed by the interpreter.
This step does not add the original module source to the final CMake library.

### 2. Add the implementation to the CMake target

Add the new source files to the `iot_runtime` library in
[`CMakeLists.txt`](../CMakeLists.txt):

```cmake
add_library(iot_runtime STATIC
  # Existing sources...
  micropython_iot_modules/sensor/sensor_cpp_bridge.cpp
  micropython_iot_modules/sensor/mod_iot_sensor.c
)
```

If the C or C++ files include headers from the new directory, also add its path
to the private include directories:

```cmake
target_include_directories(iot_runtime
  PRIVATE
    # Existing directories...
    "${PROJECT_SOURCE_DIR}/micropython_iot_modules/sensor"
)
```

These CMake entries compile the implementation into `iot_runtime`. Both this
step and `micropython.mk` are needed: one generates MicroPython's lookup tables,
and the other compiles the functions those tables refer to.

### 3. Define and register the native module

The C source needs a globals table, a module object, and a registration entry:

```c
#include "py/runtime.h"

static const mp_rom_map_elem_t sensor_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_sensor)},
};

static MP_DEFINE_CONST_DICT(
    sensor_module_globals,
    sensor_module_globals_table
);

const mp_obj_module_t iot_private_sensor_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&sensor_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_sensor, iot_private_sensor_module);
```

`MP_REGISTER_MODULE` makes the module part of the interpreter executable. It is
a built-in module, so disabling external file imports does not prevent it from
being used.

### 4. Expose the module through `iot`

Project applications use one public module:

```python
from iot import sensor
```

To follow that design, declare the private module in
[`mod_iot.c`](../micropython_iot_modules/device/mod_iot.c) and add it to the
public `iot` module's globals table. Follow the existing `display`, `input`,
`network`, `scheduler`, and `system` entries in that file.

A completely independent top-level module can instead register its public name
directly, but adding project functionality under `iot` keeps the Python API
consistent.

## Rebuilding and checking the change

Build the application for the development computer:

```bash
make iot-app
```

Build and run the unit tests:

```bash
make test
```

Cross-compile only the application for an existing image build:

```bash
make buildroot-app
```

or:

```bash
make yocto-app
```

Build a complete image only when the new module needs to be tested as part of
device startup or requires another system package.

## Common mistakes

- Adding a new C module only to `mpconfigport.h`. That file selects existing
  features; it does not add source files.
- Adding a native module to `micropython.mk` but not to the `iot_runtime` CMake
  source list. MicroPython can generate the module table, but the linker will
  not find the module implementation.
- Editing generated MicroPython files in the build directory. They will be
  overwritten.
- Editing the pinned MicroPython submodule for project-specific behaviour.
  Keep IoT App modules under `micropython_iot_modules` instead.
- Enabling an upstream module without checking its required platform
  functions.
- Expecting an added native module to appear without rebuilding IoT App.
- Expecting normal file-based imports to work while
  `MICROPY_ENABLE_EXTERNAL_IMPORT` is disabled.
