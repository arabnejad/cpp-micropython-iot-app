#pragma once

#include "iot/python/micropython_application_context.h"

namespace iot {
namespace python {
namespace internal {

/*
 * Returns the context for the Python application that is currently running.
 * The module name is included in the error when no application is active.
 *
 * The missing-context check protects against a bridge function being called
 * when no Python application is running. It also catches mistakes in the
 * application startup or shutdown order. Unit tests can trigger the same check
 * when they call a bridge function without first creating a context.
 */
MicroPythonApplicationContext &requireActiveApplicationContext(const char *moduleName);

} // namespace internal
} // namespace python
} // namespace iot
