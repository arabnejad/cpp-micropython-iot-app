#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runs Python source from C, where MicroPython exceptions can be caught safely.
 * It returns 1 on success. On failure it prints the exception, copies the end
 * of the traceback into the provided buffer, and returns 0.
 */
int iot_micropython_execute_source(const char *file_name, const char *source, size_t source_size,
                                   char *traceback_buffer, size_t traceback_buffer_size);

#ifdef __cplusplus
}
#endif
