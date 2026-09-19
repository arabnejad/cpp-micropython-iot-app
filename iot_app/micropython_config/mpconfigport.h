#pragma once

/*
 * This is the compile-time configuration for the MicroPython interpreter
 * embedded in IoT App. A value of 1 normally enables a feature and 0 disables
 * it. These choices affect the interpreter binary; a Python application cannot
 * change them while it is running.
 *
 * Each #define gives a name a value before the C compiler processes the
 * MicroPython source. MicroPython checks these names with preprocessor
 * conditions and includes only the selected code in the finished executable.
 *
 * pragma once stops this header being processed more than once in the same C
 * or C++ source file. Without that protection, repeated definitions from an
 * indirect include could cause compiler errors.
 */

/*
 * Start with the common definitions supplied by MicroPython's embed port. That
 * header defines platform types such as mp_off_t, includes the system alloca
 * declaration, and selects the embed port's hardware-abstraction header. The
 * IoT App settings below then choose the interpreter features it needs.
 */
#include <port/mpconfigport_common.h>

/*
 * Use MicroPython's core feature set as the baseline. A ROM level is a group of
 * default settings originally designed to balance firmware size against
 * available features: minimum, core, basic, extra, full, or everything. Core
 * keeps the embedded interpreter small while retaining the main Python
 * language features. Every explicit setting below overrides its baseline
 * default when needed by IoT App.
 */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

/*
 * Include the MicroPython source compiler. IoT App receives main.py as text,
 * so the interpreter must be able to parse that text and compile it to
 * bytecode before running the application.
 */
#define MICROPY_ENABLE_COMPILER (1)

/*
 * Do not load Python modules from files through the normal import mechanism.
 * Applications can still import built-in modules, including the native iot
 * module compiled into the interpreter. IoT App loads the selected main.py
 * itself and gives its source directly to MicroPython.
 */
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0)

/*
 * Allow the garbage collector to run an object's final cleanup method, such as
 * __del__, before reclaiming that object. This is separate from enabling the
 * garbage collector itself.
 */
#define MICROPY_ENABLE_FINALISER (1)

/*
 * Build MicroPython's garbage collector. Python objects are created inside the
 * interpreter heap, and the collector reclaims objects that an application can
 * no longer reach.
 */
#define MICROPY_ENABLE_GC (1)

/*
 * Store source-line information with compiled bytecode. When Python raises an
 * exception, its traceback can then identify the line in main.py that failed.
 * This uses some extra memory but makes application errors much easier to find.
 */
#define MICROPY_ENABLE_SOURCE_LINE (1)

/*
 * Allow formatted string literals such as f"Temperature: {value}". F-strings
 * make application text easier to read than joining several strings or using
 * the older percent-formatting syntax.
 */
#define MICROPY_PY_FSTRINGS (1)

/*
 * Include MicroPython's most descriptive exception messages. Detailed errors
 * can include useful context such as object, type, or function names. IoT App
 * writes these messages to its log and shows application failures on the
 * emergency screen, so the additional detail is more useful than the small
 * reduction in executable size provided by the normal reporting level.
 */
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_DETAILED)

/*
 * Store Python floating-point values as C double values. On the supported
 * Linux targets this provides 64-bit floating-point precision. System
 * temperatures and load averages are therefore returned as normal Python
 * floating-point values without reducing them to single precision.
 */
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)

/*
 * Use C long long storage when an integer is too large for MicroPython's small
 * integer representation. This supports the 64-bit-sized memory and storage
 * byte counts reported by IoT App without adding MicroPython's larger
 * arbitrary-precision MPZ implementation.
 */
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_LONGLONG)

/*
 * Expose the Python gc module. MICROPY_ENABLE_GC above builds the collector;
 * this separate setting allows Python code to call functions such as
 * gc.collect() and gc.mem_free().
 */
#define MICROPY_PY_GC (1)

/*
 * Do not include MicroPython's io module. Applications do not open arbitrary
 * files or create standard Python file streams; access to system resources is
 * provided through the native iot APIs instead.
 */
#define MICROPY_PY_IO (0)

/*
 * Include the Python sys module. It provides interpreter information and
 * standard values such as sys.modules, sys.path, sys.argv, and sys.platform.
 */
#define MICROPY_PY_SYS (1)

/* Return "iot-app" from sys.platform so Python code can identify this port. */
#define MICROPY_PY_SYS_PLATFORM "iot-app"
