#include "micropython_traceback.h"

#include "py/runtime.h"

#include <string.h>

typedef struct {
  char  *buffer;
  size_t buffer_size;
  size_t used_size;
} traceback_capture_t;

/** Prints one traceback piece and keeps its newest text in the buffer. */
static void capture_traceback_text(void *capture_data, const char *text, size_t text_size) {
  traceback_capture_t *capture = (traceback_capture_t *)capture_data;
  mp_plat_print.print_strn(mp_plat_print.data, text, text_size);

  if (capture->buffer_size <= 1U) {
    return;
  }

  const size_t text_capacity = capture->buffer_size - 1U;
  if (text_size >= text_capacity) {
    memcpy(capture->buffer, text + text_size - text_capacity, text_capacity);
    capture->used_size             = text_capacity;
    capture->buffer[text_capacity] = '\0';
    return;
  }

  const size_t required_size = capture->used_size + text_size;
  if (required_size > text_capacity) {
    const size_t discarded_size = required_size - text_capacity;
    memmove(capture->buffer, capture->buffer + discarded_size, capture->used_size - discarded_size);
    capture->used_size -= discarded_size;
  }

  memcpy(capture->buffer + capture->used_size, text, text_size);
  capture->used_size += text_size;
  capture->buffer[capture->used_size] = '\0';
}

void iot_clear_micropython_traceback(char *traceback_buffer, size_t traceback_buffer_size) {
  if (traceback_buffer_size > 0U) {
    traceback_buffer[0] = '\0';
  }
}

void iot_print_and_capture_micropython_exception(mp_obj_t exception, char *traceback_buffer,
                                                 size_t traceback_buffer_size) {
  traceback_capture_t capture = {traceback_buffer, traceback_buffer_size, 0U};
  const mp_print_t    printer = {&capture, capture_traceback_text};
  mp_obj_print_exception(&printer, exception);
}
