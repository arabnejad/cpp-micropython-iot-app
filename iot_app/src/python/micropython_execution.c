#include "micropython_execution.h"

#include "micropython_traceback.h"

#include "py/compile.h"
#include "py/lexer.h"
#include "py/nlr.h"
#include "py/runtime.h"

int iot_micropython_execute_source(const char *file_name, const char *source, size_t source_size,
                                   char *traceback_buffer, size_t traceback_buffer_size) {
  iot_clear_micropython_traceback(traceback_buffer, traceback_buffer_size);

  nlr_buf_t exception_handler;
  if (nlr_push(&exception_handler) == 0) {
    const qstr      source_name     = qstr_from_str(file_name);
    mp_lexer_t     *lexer           = mp_lexer_new_from_str_len(source_name, source, source_size, 0);
    mp_parse_tree_t parsed_source   = mp_parse(lexer, MP_PARSE_FILE_INPUT);
    mp_obj_t        executable_code = mp_compile(&parsed_source, source_name, false);
    mp_call_function_0(executable_code);
    nlr_pop();
    return 1;
  }

  // Catch the exception in C so MicroPython does not jump over live C++
  // objects. Keep a copy for the emergency screen as well.
  iot_print_and_capture_micropython_exception((mp_obj_t)exception_handler.ret_val, traceback_buffer,
                                              traceback_buffer_size);
  return 0;
}
