#include "iot_scheduler_runtime.h"

#include "micropython_traceback.h"

#include "py/nlr.h"
#include "py/objlist.h"
#include "py/runtime.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One Python app can keep at most this many active timers. */
#define IOT_SCHEDULER_MAXIMUM_TASK_COUNT (128U)

/*
 * One repeating callback registered by scheduler.every().
 *
 * MicroPython's VM keeps the head of this linked list. This prevents the timer
 * and its Python callback from being collected after main.py returns.
 */
typedef struct _iot_scheduled_task_t {
  struct _iot_scheduled_task_t *next;
  mp_obj_t                      callback;
  uint32_t                      task_id;
  uint32_t                      interval_milliseconds;
  uint32_t                      remaining_milliseconds;
} iot_scheduled_task_t;

// MicroPython puts this declaration in a generated header used by the VM. Keep
// it as void * there because the task structure belongs only to this file.
MP_REGISTER_ROOT_POINTER(void *iot_scheduler_task_head);

static uint32_t next_task_id = 1U;

static iot_scheduled_task_t *scheduler_task_head(void) {
  return (iot_scheduled_task_t *)MP_STATE_VM(iot_scheduler_task_head);
}

void iot_scheduler_reset(void) {
  MP_STATE_VM(iot_scheduler_task_head) = NULL;
  next_task_id                         = 1U;
}

static size_t scheduler_task_count(void) {
  size_t count = 0U;
  for (const iot_scheduled_task_t *task = scheduler_task_head(); task != NULL; task = task->next) {
    ++count;
  }
  return count;
}

static mp_obj_t scheduler_every(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                                mp_map_t *keyword_arguments) {
  enum { ARG_milliseconds, ARG_callback };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_milliseconds, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_callback, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all(number_of_arguments, positional_arguments, keyword_arguments, MP_ARRAY_SIZE(allowed_arguments),
                   allowed_arguments, arguments);

  const mp_int_t interval_milliseconds = arguments[ARG_milliseconds].u_int;
  if (interval_milliseconds <= 0 || (mp_uint_t)interval_milliseconds > UINT32_MAX) {
    mp_raise_ValueError(MP_ERROR_TEXT("milliseconds must be between 1 and 4294967295"));
  }
  if (!mp_obj_is_callable(arguments[ARG_callback].u_obj)) {
    mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
  }
  if (scheduler_task_count() >= IOT_SCHEDULER_MAXIMUM_TASK_COUNT) {
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("an application may schedule at most 128 timers"));
  }

  iot_scheduled_task_t *task = m_new_obj(iot_scheduled_task_t);
  task->next                 = scheduler_task_head();
  task->callback             = arguments[ARG_callback].u_obj;
  task->task_id              = next_task_id++;
  if (next_task_id == 0U) {
    next_task_id = 1U;
  }
  task->interval_milliseconds          = (uint32_t)interval_milliseconds;
  task->remaining_milliseconds         = task->interval_milliseconds;
  MP_STATE_VM(iot_scheduler_task_head) = task;
  return mp_obj_new_int_from_uint(task->task_id);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(scheduler_every_object, 0, scheduler_every);

static mp_obj_t scheduler_cancel(mp_obj_t task_id_object) {
  const mp_int_t requested_task_id = mp_obj_get_int(task_id_object);
  if (requested_task_id <= 0 || (mp_uint_t)requested_task_id > UINT32_MAX) {
    return mp_const_false;
  }

  iot_scheduled_task_t *previous_task = NULL;
  iot_scheduled_task_t *task          = scheduler_task_head();
  while (task != NULL) {
    if (task->task_id == (uint32_t)requested_task_id) {
      if (previous_task == NULL) {
        MP_STATE_VM(iot_scheduler_task_head) = task->next;
      } else {
        previous_task->next = task->next;
      }
      return mp_const_true;
    }
    previous_task = task;
    task          = task->next;
  }
  return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(scheduler_cancel_object, scheduler_cancel);

static mp_obj_t scheduler_clear(void) {
  MP_STATE_VM(iot_scheduler_task_head) = NULL;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(scheduler_clear_object, scheduler_clear);

int iot_scheduler_next_delay_milliseconds(uint32_t *delay_milliseconds) {
  if (delay_milliseconds == NULL || scheduler_task_head() == NULL) {
    return 0;
  }

  uint32_t nearest_delay = UINT32_MAX;
  for (const iot_scheduled_task_t *task = scheduler_task_head(); task != NULL; task = task->next) {
    if (task->remaining_milliseconds < nearest_delay) {
      nearest_delay = task->remaining_milliseconds;
    }
  }
  *delay_milliseconds = nearest_delay;
  return 1;
}

int iot_scheduler_run_due_callbacks(uint32_t elapsed_milliseconds, char *traceback_buffer,
                                    size_t traceback_buffer_size) {
  iot_clear_micropython_traceback(traceback_buffer, traceback_buffer_size);

  nlr_buf_t exception_handler;
  if (nlr_push(&exception_handler) == 0) {
    // Copy due callbacks before running them. A callback can then add or cancel
    // timers without changing the list currently being processed.
    mp_obj_t due_callbacks = mp_obj_new_list(0U, NULL);

    for (iot_scheduled_task_t *task = scheduler_task_head(); task != NULL; task = task->next) {
      if (elapsed_milliseconds < task->remaining_milliseconds) {
        task->remaining_milliseconds -= elapsed_milliseconds;
        continue;
      }

      const uint32_t time_after_deadline = elapsed_milliseconds - task->remaining_milliseconds;
      const uint32_t interval_overrun    = time_after_deadline % task->interval_milliseconds;
      task->remaining_milliseconds       = task->interval_milliseconds - interval_overrun;
      mp_obj_list_append(due_callbacks, task->callback);
    }

    size_t    callback_count = 0U;
    mp_obj_t *callback_items = NULL;
    mp_obj_get_array(due_callbacks, &callback_count, &callback_items);
    for (size_t index = 0U; index < callback_count; ++index) {
      mp_call_function_0(callback_items[index]);
    }
    nlr_pop();
    return 1;
  }

  iot_print_and_capture_micropython_exception((mp_obj_t)exception_handler.ret_val, traceback_buffer,
                                              traceback_buffer_size);
  return 0;
}

static const mp_rom_map_elem_t scheduler_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_scheduler)},
    {MP_ROM_QSTR(MP_QSTR_every), MP_ROM_PTR(&scheduler_every_object)},
    {MP_ROM_QSTR(MP_QSTR_cancel), MP_ROM_PTR(&scheduler_cancel_object)},
    {MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&scheduler_clear_object)},
};
static MP_DEFINE_CONST_DICT(scheduler_module_globals, scheduler_module_globals_table);

const mp_obj_module_t iot_private_scheduler_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&scheduler_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_scheduler, iot_private_scheduler_module);
