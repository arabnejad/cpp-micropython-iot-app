#pragma once

#include "iot_native_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raises a MicroPython RuntimeError when a native C++ call failed. */
void iot_raise_native_error(iot_native_result_t native_call_result);

#ifdef __cplusplus
}
#endif
