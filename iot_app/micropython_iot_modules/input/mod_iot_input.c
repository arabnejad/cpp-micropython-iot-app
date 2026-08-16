#include "input_cpp_bridge.h"

#include "py/runtime.h"

#include <limits.h>
#include <string.h>

typedef struct {
  mp_obj_base_t base;
  void         *native_handle;
} iot_gamepad_object_t;

/** Python view of joystick or button data that keeps its gamepad alive. */
typedef struct {
  mp_obj_base_t base;
  mp_obj_t      gamepad_owner;
} iot_gamepad_state_view_object_t;

static void raise_native_error(iot_native_result_t nativeCallResult) {
  if (!nativeCallResult.succeeded) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), nativeCallResult.error_message);
  }
}

static iot_gamepad_object_t *gamepad_object(mp_obj_t object) {
  iot_gamepad_object_t *gamepad = MP_OBJ_TO_PTR(object);
  if (gamepad->native_handle == NULL) {
    mp_raise_ValueError(MP_ERROR_TEXT("gamepad is closed"));
  }
  return gamepad;
}

static mp_obj_t gamepad_make_new(const mp_obj_type_t *type, size_t number_of_positional_arguments,
                                 size_t number_of_keyword_arguments, const mp_obj_t *all_arguments) {
  enum { ARG_i2c_bus_number, ARG_i2c_address };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_i2c_bus_number, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_i2c_address, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all_kw_array(number_of_positional_arguments, number_of_keyword_arguments, all_arguments,
                            MP_ARRAY_SIZE(allowed_arguments), allowed_arguments, arguments);

  if (arguments[ARG_i2c_address].u_int < 0 || arguments[ARG_i2c_address].u_int > 0x7f) {
    mp_raise_ValueError(MP_ERROR_TEXT("i2c_address must be a seven-bit address between 0 and 0x7f"));
  }
  if (arguments[ARG_i2c_bus_number].u_int < INT_MIN || arguments[ARG_i2c_bus_number].u_int > INT_MAX) {
    mp_raise_ValueError(MP_ERROR_TEXT("i2c_bus_number is outside the supported integer range"));
  }

  // Allocate the Python wrapper before the C++ gamepad. If allocation fails,
  // there is no native object to clean up.
  iot_gamepad_object_t *self = mp_obj_malloc_with_finaliser(iot_gamepad_object_t, type);
  self->native_handle        = NULL;

  iot_native_pointer_result_t gamepadCreationResult =
      iot_gamepad_create((int)arguments[ARG_i2c_bus_number].u_int, (uint8_t)arguments[ARG_i2c_address].u_int);
  if (gamepadCreationResult.value == NULL) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), gamepadCreationResult.error_message);
  }

  self->native_handle = gamepadCreationResult.value;
  return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t gamepad_close(mp_obj_t self_in) {
  iot_gamepad_object_t *self = MP_OBJ_TO_PTR(self_in);
  if (self->native_handle != NULL) {
    iot_gamepad_destroy(self->native_handle);
    self->native_handle = NULL;
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_close_object, gamepad_close);

static mp_obj_t gamepad_model_name(mp_obj_t self_in) {
  const char *model_name = NULL;
  raise_native_error(iot_gamepad_model_name(gamepad_object(self_in)->native_handle, &model_name));
  return mp_obj_new_str(model_name, strlen(model_name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_model_name_object, gamepad_model_name);

static mp_obj_t gamepad_connect(mp_obj_t self_in) {
  raise_native_error(iot_gamepad_connect(gamepad_object(self_in)->native_handle));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_connect_object, gamepad_connect);

static mp_obj_t gamepad_calibrate_joystick(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                                           mp_map_t *keyword_arguments) {
  enum { ARG_number_of_samples, ARG_dead_zone };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_number_of_samples, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 20}},
      {MP_QSTR_dead_zone, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all(number_of_arguments - 1U, positional_arguments + 1, keyword_arguments,
                   MP_ARRAY_SIZE(allowed_arguments), allowed_arguments, arguments);

  if (arguments[ARG_number_of_samples].u_int <= 0) {
    mp_raise_ValueError(MP_ERROR_TEXT("number_of_samples must be greater than zero"));
  }
  if (arguments[ARG_dead_zone].u_int < INT_MIN || arguments[ARG_dead_zone].u_int > INT_MAX) {
    mp_raise_ValueError(MP_ERROR_TEXT("dead_zone is outside the supported integer range"));
  }

  iot_gamepad_object_t *self = gamepad_object(positional_arguments[0]);
  raise_native_error(iot_gamepad_calibrate_joystick(self->native_handle, (size_t)arguments[ARG_number_of_samples].u_int,
                                                    (int)arguments[ARG_dead_zone].u_int));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(gamepad_calibrate_joystick_object, 1, gamepad_calibrate_joystick);

static mp_obj_t gamepad_refresh_input_state(mp_obj_t self_in) {
  raise_native_error(iot_gamepad_refresh_input_state(gamepad_object(self_in)->native_handle));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_refresh_input_state_object, gamepad_refresh_input_state);

static mp_obj_t gamepad_is_connected(mp_obj_t self_in) {
  int is_connected = 0;
  raise_native_error(iot_gamepad_is_connected(gamepad_object(self_in)->native_handle, &is_connected));
  return mp_obj_new_bool(is_connected != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_is_connected_object, gamepad_is_connected);

static iot_gamepad_object_t *owner_from_state_view(mp_obj_t state_view_in) {
  iot_gamepad_state_view_object_t *state_view = MP_OBJ_TO_PTR(state_view_in);
  return gamepad_object(state_view->gamepad_owner);
}

static iot_gamepad_state_t read_state_from_view(mp_obj_t state_view_in) {
  iot_gamepad_state_t state = {0};
  raise_native_error(iot_gamepad_read_state(owner_from_state_view(state_view_in)->native_handle, &state));
  return state;
}

static mp_obj_t joystick_position(mp_obj_t self_in) {
  const iot_gamepad_state_t state      = read_state_from_view(self_in);
  mp_obj_t                  position[] = {mp_obj_new_int(state.x), mp_obj_new_int(state.y)};
  return mp_obj_new_tuple(MP_ARRAY_SIZE(position), position);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_position_object, joystick_position);

static mp_obj_t joystick_centre(mp_obj_t self_in) {
  const iot_gamepad_state_t state    = read_state_from_view(self_in);
  mp_obj_t                  centre[] = {mp_obj_new_int(state.centre_x), mp_obj_new_int(state.centre_y)};
  return mp_obj_new_tuple(MP_ARRAY_SIZE(centre), centre);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_centre_object, joystick_centre);

static mp_obj_t joystick_dead_zone(mp_obj_t self_in) {
  return mp_obj_new_int(read_state_from_view(self_in).dead_zone);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_dead_zone_object, joystick_dead_zone);

static mp_obj_t joystick_direction(mp_obj_t self_in) {
  const char *direction = NULL;
  raise_native_error(iot_gamepad_joystick_direction(owner_from_state_view(self_in)->native_handle, &direction));
  return mp_obj_new_str(direction, strlen(direction));
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_direction_object, joystick_direction);

static mp_obj_t buttons_pressed(mp_obj_t self_in) {
  static const qstr button_names[] = {
      MP_QSTR_X, MP_QSTR_Y, MP_QSTR_A, MP_QSTR_B, MP_QSTR_Select, MP_QSTR_Start,
  };
  const uint32_t pressed_mask = read_state_from_view(self_in).pressed_buttons_mask;
  mp_obj_t       pressed_buttons[MP_ARRAY_SIZE(button_names)];
  size_t         number_of_pressed_buttons = 0;
  for (size_t button_index = 0; button_index < MP_ARRAY_SIZE(button_names); ++button_index) {
    if ((pressed_mask & (UINT32_C(1) << button_index)) != 0U) {
      pressed_buttons[number_of_pressed_buttons++] = MP_OBJ_NEW_QSTR(button_names[button_index]);
    }
  }
  return mp_obj_new_tuple(number_of_pressed_buttons, pressed_buttons);
}
static MP_DEFINE_CONST_FUN_OBJ_1(buttons_pressed_object, buttons_pressed);

static mp_obj_t buttons_is_pressed(mp_obj_t self_in, mp_obj_t button_name_in) {
  const qstr button_name = mp_obj_str_get_qstr(button_name_in);
  size_t     button_index;
  switch (button_name) {
  case MP_QSTR_X:
    button_index = 0U;
    break;
  case MP_QSTR_Y:
    button_index = 1U;
    break;
  case MP_QSTR_A:
    button_index = 2U;
    break;
  case MP_QSTR_B:
    button_index = 3U;
    break;
  case MP_QSTR_Select:
    button_index = 4U;
    break;
  case MP_QSTR_Start:
    button_index = 5U;
    break;
  default:
    mp_raise_ValueError(MP_ERROR_TEXT("unknown gamepad button name"));
  }

  const uint32_t pressed_mask = read_state_from_view(self_in).pressed_buttons_mask;
  return mp_obj_new_bool((pressed_mask & (UINT32_C(1) << button_index)) != 0U);
}
static MP_DEFINE_CONST_FUN_OBJ_2(buttons_is_pressed_object, buttons_is_pressed);

static const mp_rom_map_elem_t joystick_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_position), MP_ROM_PTR(&joystick_position_object)},
    {MP_ROM_QSTR(MP_QSTR_centre), MP_ROM_PTR(&joystick_centre_object)},
    {MP_ROM_QSTR(MP_QSTR_dead_zone), MP_ROM_PTR(&joystick_dead_zone_object)},
    {MP_ROM_QSTR(MP_QSTR_direction), MP_ROM_PTR(&joystick_direction_object)},
};
static MP_DEFINE_CONST_DICT(joystick_locals, joystick_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(iot_gamepad_joystick_type, MP_QSTR_GamepadJoystick, MP_TYPE_FLAG_NONE, locals_dict,
                         &joystick_locals);

static const mp_rom_map_elem_t buttons_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_pressed), MP_ROM_PTR(&buttons_pressed_object)},
    {MP_ROM_QSTR(MP_QSTR_is_pressed), MP_ROM_PTR(&buttons_is_pressed_object)},
};
static MP_DEFINE_CONST_DICT(buttons_locals, buttons_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(iot_gamepad_buttons_type, MP_QSTR_GamepadButtons, MP_TYPE_FLAG_NONE, locals_dict,
                         &buttons_locals);

static mp_obj_t create_state_view(mp_obj_t gamepad_owner, const mp_obj_type_t *view_type) {
  gamepad_object(gamepad_owner);
  iot_gamepad_state_view_object_t *view = mp_obj_malloc(iot_gamepad_state_view_object_t, view_type);
  view->gamepad_owner                   = gamepad_owner;
  return MP_OBJ_FROM_PTR(view);
}

static mp_obj_t gamepad_joystick(mp_obj_t self_in) {
  return create_state_view(self_in, &iot_gamepad_joystick_type);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_joystick_object, gamepad_joystick);

static mp_obj_t gamepad_buttons(mp_obj_t self_in) {
  return create_state_view(self_in, &iot_gamepad_buttons_type);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_buttons_object, gamepad_buttons);

static iot_gamepad_device_information_t read_diagnostics(mp_obj_t self_in) {
  iot_gamepad_device_information_t diagnostics = {0};
  raise_native_error(iot_gamepad_read_diagnostics(gamepad_object(self_in)->native_handle, &diagnostics));
  return diagnostics;
}

static mp_obj_t gamepad_processor_hardware_id(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).processor_hardware_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_processor_hardware_id_object, gamepad_processor_hardware_id);

static mp_obj_t gamepad_firmware_product_id(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).firmware_product_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_firmware_product_id_object, gamepad_firmware_product_id);

static mp_obj_t gamepad_firmware_date_code(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).firmware_date_code);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_firmware_date_code_object, gamepad_firmware_date_code);

static mp_obj_t gamepad_combined_product_id_and_firmware_date_code(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).combined_product_id_and_firmware_date_code);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_combined_product_id_and_firmware_date_code_object,
                                 gamepad_combined_product_id_and_firmware_date_code);

static const mp_rom_map_elem_t gamepad_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&gamepad_close_object)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&gamepad_close_object)},
    {MP_ROM_QSTR(MP_QSTR_model_name), MP_ROM_PTR(&gamepad_model_name_object)},
    {MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&gamepad_connect_object)},
    {MP_ROM_QSTR(MP_QSTR_calibrate_joystick), MP_ROM_PTR(&gamepad_calibrate_joystick_object)},
    {MP_ROM_QSTR(MP_QSTR_refresh_input_state), MP_ROM_PTR(&gamepad_refresh_input_state_object)},
    {MP_ROM_QSTR(MP_QSTR_is_connected), MP_ROM_PTR(&gamepad_is_connected_object)},
    {MP_ROM_QSTR(MP_QSTR_joystick), MP_ROM_PTR(&gamepad_joystick_object)},
    {MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&gamepad_buttons_object)},
    {MP_ROM_QSTR(MP_QSTR_processor_hardware_id), MP_ROM_PTR(&gamepad_processor_hardware_id_object)},
    {MP_ROM_QSTR(MP_QSTR_firmware_product_id), MP_ROM_PTR(&gamepad_firmware_product_id_object)},
    {MP_ROM_QSTR(MP_QSTR_firmware_date_code), MP_ROM_PTR(&gamepad_firmware_date_code_object)},
    {MP_ROM_QSTR(MP_QSTR_combined_product_id_and_firmware_date_code),
     MP_ROM_PTR(&gamepad_combined_product_id_and_firmware_date_code_object)},
};
static MP_DEFINE_CONST_DICT(gamepad_locals, gamepad_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(iot_gamepad_type, MP_QSTR_AdafruitMiniI2cGamepad, MP_TYPE_FLAG_NONE, make_new,
                         gamepad_make_new, locals_dict, &gamepad_locals);

static const mp_rom_map_elem_t input_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_input)},
    {MP_ROM_QSTR(MP_QSTR_AdafruitMiniI2cGamepad), MP_ROM_PTR(&iot_gamepad_type)},
    {MP_ROM_QSTR(MP_QSTR_GamepadJoystick), MP_ROM_PTR(&iot_gamepad_joystick_type)},
    {MP_ROM_QSTR(MP_QSTR_GamepadButtons), MP_ROM_PTR(&iot_gamepad_buttons_type)},
};
static MP_DEFINE_CONST_DICT(input_module_globals, input_module_globals_table);

const mp_obj_module_t iot_private_input_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&input_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_input, iot_private_input_module);
