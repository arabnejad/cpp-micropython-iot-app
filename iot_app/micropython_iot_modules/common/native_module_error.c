#include "native_module_error.h"

#include "py/runtime.h"

void iot_raise_native_error(iot_native_result_t native_call_result) {
  if (native_call_result.succeeded) {
    return;
  }

  const char *error_message = native_call_result.error_message;
  if (error_message == NULL || error_message[0] == '\0') {
    error_message = "Unknown native module error";
  }
  mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), error_message);
}
