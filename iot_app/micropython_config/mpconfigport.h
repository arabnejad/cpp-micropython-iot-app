#pragma once

#include <port/mpconfigport_common.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0)
#define MICROPY_ENABLE_FINALISER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_SOURCE_LINE (1)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
// System temperatures and load averages are exposed as normal Python floats.
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
// Memory and storage byte counts can be larger than a MicroPython small int.
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_LONGLONG)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_IO (0)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_PLATFORM "iot-app"
