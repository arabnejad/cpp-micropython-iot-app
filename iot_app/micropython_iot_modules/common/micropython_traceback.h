#pragma once

#include "py/obj.h"

#include <stddef.h>

/* Empties the traceback buffer before running MicroPython code. */
void iot_clear_micropython_traceback(char *traceback_buffer, size_t traceback_buffer_size);

/*
 * Prints a MicroPython exception and saves the end of its traceback.
 *
 * The terminal still receives the full traceback. If the buffer is too small,
 * old text is dropped and the final lines are kept for the emergency screen.
 */
void iot_print_and_capture_micropython_exception(mp_obj_t exception, char *traceback_buffer,
                                                 size_t traceback_buffer_size);
