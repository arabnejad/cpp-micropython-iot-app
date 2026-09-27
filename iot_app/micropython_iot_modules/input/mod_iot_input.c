#include "input_cpp_bridge.h"
#include "native_module_error.h"

#include "py/runtime.h"

#include <string.h>

typedef struct {
  mp_obj_base_t base;
  void         *native_handle;
} iot_controller_object_t;

/* Python view of joystick or button data that keeps its controller alive. */
typedef struct {
  mp_obj_base_t base;
  mp_obj_t      controller_owner;
} iot_controller_state_view_object_t;

static iot_controller_object_t *controller_object(mp_obj_t object) {
  iot_controller_object_t *controller = MP_OBJ_TO_PTR(object);
  if (controller->native_handle == NULL) {
    mp_raise_ValueError(MP_ERROR_TEXT("controller is closed"));
  }
  return controller;
}

static mp_obj_t adafruit_controller_make_new(const mp_obj_type_t *type, size_t number_of_positional_arguments,
                                             size_t number_of_keyword_arguments, const mp_obj_t *all_arguments) {
  (void)all_arguments;
  mp_arg_check_num(number_of_positional_arguments, number_of_keyword_arguments, 0, 0, false);

  // Allocate the Python wrapper before the C++ controller. If allocation fails,
  // there is no native object to clean up.
  iot_controller_object_t *self = mp_obj_malloc_with_finaliser(iot_controller_object_t, type);
  self->native_handle           = NULL;

  iot_native_pointer_result_t adafruitControllerCreationResult = iot_adafruit_controller_create();
  if (adafruitControllerCreationResult.value == NULL) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), adafruitControllerCreationResult.error_message);
  }

  self->native_handle = adafruitControllerCreationResult.value;
  return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t seengreat_controller_make_new(const mp_obj_type_t *type, size_t positional_count, size_t keyword_count,
                                              const mp_obj_t *arguments) {
  (void)arguments;
  mp_arg_check_num(positional_count, keyword_count, 0, 0, false);
  iot_controller_object_t *self = mp_obj_malloc_with_finaliser(iot_controller_object_t, type);
  self->native_handle           = NULL;
  const iot_native_pointer_result_t seenGreatControllerCreationResult = iot_seengreat_controller_create();
  if (seenGreatControllerCreationResult.value == NULL) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), seenGreatControllerCreationResult.error_message);
  }
  self->native_handle = seenGreatControllerCreationResult.value;
  return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t controller_close(mp_obj_t self_in) {
  iot_controller_object_t *self = MP_OBJ_TO_PTR(self_in);
  if (self->native_handle != NULL) {
    iot_controller_destroy(self->native_handle);
    self->native_handle = NULL;
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_close_object, controller_close);

static mp_obj_t controller_model_name(mp_obj_t self_in) {
  const char *model_name = NULL;
  iot_raise_native_error(iot_controller_model_name(controller_object(self_in)->native_handle, &model_name));
  return mp_obj_new_str(model_name, strlen(model_name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_model_name_object, controller_model_name);

static mp_obj_t controller_board_type(mp_obj_t self_in) {
  const char *board_type = NULL;
  iot_raise_native_error(iot_controller_board_type(controller_object(self_in)->native_handle, &board_type));
  return mp_obj_new_str(board_type, strlen(board_type));
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_board_type_object, controller_board_type);

static mp_obj_t controller_connect(mp_obj_t self_in) {
  iot_raise_native_error(iot_controller_connect(controller_object(self_in)->native_handle));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_connect_object, controller_connect);

static mp_obj_t adafruit_controller_calibrate_joystick(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                                                       mp_map_t *keyword_arguments) {
  /* Gives a readable name to each position in the parsed argument array. */
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

  iot_controller_object_t *self = controller_object(positional_arguments[0]);
  iot_raise_native_error(iot_adafruit_controller_calibrate_joystick(
      self->native_handle, (size_t)arguments[ARG_number_of_samples].u_int, (int)arguments[ARG_dead_zone].u_int));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(adafruit_controller_calibrate_joystick_object, 1,
                                  adafruit_controller_calibrate_joystick);

static mp_obj_t controller_refresh_input_state(mp_obj_t self_in) {
  iot_raise_native_error(iot_controller_refresh_input_state(controller_object(self_in)->native_handle));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_refresh_input_state_object, controller_refresh_input_state);

static mp_obj_t controller_is_connected(mp_obj_t self_in) {
  int is_connected = 0;
  iot_raise_native_error(iot_controller_is_connected(controller_object(self_in)->native_handle, &is_connected));
  return mp_obj_new_bool(is_connected != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_is_connected_object, controller_is_connected);

static iot_controller_object_t *owner_from_state_view(mp_obj_t state_view_in) {
  iot_controller_state_view_object_t *state_view = MP_OBJ_TO_PTR(state_view_in);
  return controller_object(state_view->controller_owner);
}

static iot_controller_state_t read_state_from_view(mp_obj_t state_view_in) {
  iot_controller_state_t state = {0};
  iot_raise_native_error(iot_controller_read_state(owner_from_state_view(state_view_in)->native_handle, &state));
  return state;
}

static mp_obj_t joystick_position(mp_obj_t self_in) {
  const iot_controller_state_t state      = read_state_from_view(self_in);
  mp_obj_t                     position[] = {mp_obj_new_int(state.x), mp_obj_new_int(state.y)};
  return mp_obj_new_tuple(MP_ARRAY_SIZE(position), position);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_position_object, joystick_position);

static mp_obj_t joystick_centre(mp_obj_t self_in) {
  const iot_controller_state_t state    = read_state_from_view(self_in);
  mp_obj_t                     centre[] = {mp_obj_new_int(state.centre_x), mp_obj_new_int(state.centre_y)};
  return mp_obj_new_tuple(MP_ARRAY_SIZE(centre), centre);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_centre_object, joystick_centre);

static mp_obj_t joystick_dead_zone(mp_obj_t self_in) {
  return mp_obj_new_int(read_state_from_view(self_in).dead_zone);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_dead_zone_object, joystick_dead_zone);

static mp_obj_t joystick_direction(mp_obj_t self_in) {
  const char *direction = NULL;
  iot_raise_native_error(iot_controller_joystick_direction(owner_from_state_view(self_in)->native_handle, &direction));
  return mp_obj_new_str(direction, strlen(direction));
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_direction_object, joystick_direction);

/* This order matches the bit positions in ControllerButton. */
static const qstr controller_button_names[] = {
    MP_QSTR_X,     MP_QSTR_Y,  MP_QSTR_A,  MP_QSTR_B,  MP_QSTR_Select,
    MP_QSTR_Start, MP_QSTR_K1, MP_QSTR_K2, MP_QSTR_K3, MP_QSTR_Press,
};

static mp_obj_t buttons_pressed(mp_obj_t self_in) {
  const uint32_t pressed_mask = read_state_from_view(self_in).pressed_buttons_mask;
  mp_obj_t       pressed_buttons[MP_ARRAY_SIZE(controller_button_names)];
  size_t         number_of_pressed_buttons = 0;
  for (size_t button_index = 0; button_index < MP_ARRAY_SIZE(controller_button_names); ++button_index) {
    if ((pressed_mask & (UINT32_C(1) << button_index)) != 0U) {
      pressed_buttons[number_of_pressed_buttons++] = MP_OBJ_NEW_QSTR(controller_button_names[button_index]);
    }
  }
  return mp_obj_new_tuple(number_of_pressed_buttons, pressed_buttons);
}
static MP_DEFINE_CONST_FUN_OBJ_1(buttons_pressed_object, buttons_pressed);

static mp_obj_t buttons_is_pressed(mp_obj_t self_in, mp_obj_t button_name_in) {
  const qstr button_name = mp_obj_str_get_qstr(button_name_in);
  for (size_t button_index = 0; button_index < MP_ARRAY_SIZE(controller_button_names); ++button_index) {
    if (controller_button_names[button_index] == button_name) {
      const uint32_t pressed_mask = read_state_from_view(self_in).pressed_buttons_mask;
      return mp_obj_new_bool((pressed_mask & (UINT32_C(1) << button_index)) != 0U);
    }
  }
  mp_raise_ValueError(MP_ERROR_TEXT("unknown controller button name"));
}
static MP_DEFINE_CONST_FUN_OBJ_2(buttons_is_pressed_object, buttons_is_pressed);

static const mp_rom_map_elem_t joystick_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_position), MP_ROM_PTR(&joystick_position_object)},
    {MP_ROM_QSTR(MP_QSTR_centre), MP_ROM_PTR(&joystick_centre_object)},
    {MP_ROM_QSTR(MP_QSTR_dead_zone), MP_ROM_PTR(&joystick_dead_zone_object)},
    {MP_ROM_QSTR(MP_QSTR_direction), MP_ROM_PTR(&joystick_direction_object)},
};
static MP_DEFINE_CONST_DICT(joystick_locals, joystick_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(iot_controller_joystick_type, MP_QSTR_ControllerJoystick, MP_TYPE_FLAG_NONE, locals_dict,
                         &joystick_locals);

static const mp_rom_map_elem_t buttons_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_pressed), MP_ROM_PTR(&buttons_pressed_object)},
    {MP_ROM_QSTR(MP_QSTR_is_pressed), MP_ROM_PTR(&buttons_is_pressed_object)},
};
static MP_DEFINE_CONST_DICT(buttons_locals, buttons_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(iot_controller_buttons_type, MP_QSTR_ControllerButtons, MP_TYPE_FLAG_NONE, locals_dict,
                         &buttons_locals);

static mp_obj_t create_state_view(mp_obj_t controller_owner, const mp_obj_type_t *view_type) {
  controller_object(controller_owner);
  iot_controller_state_view_object_t *view = mp_obj_malloc(iot_controller_state_view_object_t, view_type);
  view->controller_owner                   = controller_owner;
  return MP_OBJ_FROM_PTR(view);
}

static mp_obj_t controller_joystick(mp_obj_t self_in) {
  return create_state_view(self_in, &iot_controller_joystick_type);
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_joystick_object, controller_joystick);

static mp_obj_t controller_buttons(mp_obj_t self_in) {
  return create_state_view(self_in, &iot_controller_buttons_type);
}
static MP_DEFINE_CONST_FUN_OBJ_1(controller_buttons_object, controller_buttons);

static mp_obj_t adafruit_controller_connection_information(mp_obj_t self_in) {
  iot_adafruit_controller_connection_information_t connection_information = {0};
  iot_raise_native_error(iot_adafruit_controller_read_connection_information(controller_object(self_in)->native_handle,
                                                                             &connection_information));

  mp_obj_t result = mp_obj_new_dict(3);
  mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_bus_number), mp_obj_new_int(connection_information.bus_number));
  mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_address), mp_obj_new_int(connection_information.address));
  mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_device_path),
                    mp_obj_new_str(connection_information.device_path, strlen(connection_information.device_path)));
  return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(adafruit_controller_connection_information_object,
                                 adafruit_controller_connection_information);

static iot_adafruit_controller_device_information_t read_diagnostics(mp_obj_t self_in) {
  iot_adafruit_controller_device_information_t diagnostics = {0};
  iot_raise_native_error(
      iot_adafruit_controller_read_diagnostics(controller_object(self_in)->native_handle, &diagnostics));
  return diagnostics;
}

static mp_obj_t adafruit_controller_processor_hardware_id(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).processor_hardware_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(adafruit_controller_processor_hardware_id_object,
                                 adafruit_controller_processor_hardware_id);

static mp_obj_t adafruit_controller_firmware_product_id(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).firmware_product_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(adafruit_controller_firmware_product_id_object,
                                 adafruit_controller_firmware_product_id);

static mp_obj_t adafruit_controller_firmware_date_code(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).firmware_date_code);
}
static MP_DEFINE_CONST_FUN_OBJ_1(adafruit_controller_firmware_date_code_object, adafruit_controller_firmware_date_code);

static mp_obj_t adafruit_controller_combined_product_id_and_firmware_date_code(mp_obj_t self_in) {
  return mp_obj_new_int_from_uint(read_diagnostics(self_in).combined_product_id_and_firmware_date_code);
}
static MP_DEFINE_CONST_FUN_OBJ_1(adafruit_controller_combined_product_id_and_firmware_date_code_object,
                                 adafruit_controller_combined_product_id_and_firmware_date_code);

static const mp_rom_map_elem_t adafruit_controller_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&controller_close_object)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&controller_close_object)},
    {MP_ROM_QSTR(MP_QSTR_model_name), MP_ROM_PTR(&controller_model_name_object)},
    {MP_ROM_QSTR(MP_QSTR_board_type), MP_ROM_PTR(&controller_board_type_object)},
    {MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&controller_connect_object)},
    {MP_ROM_QSTR(MP_QSTR_calibrate_joystick), MP_ROM_PTR(&adafruit_controller_calibrate_joystick_object)},
    {MP_ROM_QSTR(MP_QSTR_refresh_input_state), MP_ROM_PTR(&controller_refresh_input_state_object)},
    {MP_ROM_QSTR(MP_QSTR_is_connected), MP_ROM_PTR(&controller_is_connected_object)},
    {MP_ROM_QSTR(MP_QSTR_joystick), MP_ROM_PTR(&controller_joystick_object)},
    {MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&controller_buttons_object)},
    {MP_ROM_QSTR(MP_QSTR_connection_information), MP_ROM_PTR(&adafruit_controller_connection_information_object)},
    {MP_ROM_QSTR(MP_QSTR_processor_hardware_id), MP_ROM_PTR(&adafruit_controller_processor_hardware_id_object)},
    {MP_ROM_QSTR(MP_QSTR_firmware_product_id), MP_ROM_PTR(&adafruit_controller_firmware_product_id_object)},
    {MP_ROM_QSTR(MP_QSTR_firmware_date_code), MP_ROM_PTR(&adafruit_controller_firmware_date_code_object)},
    {MP_ROM_QSTR(MP_QSTR_combined_product_id_and_firmware_date_code),
     MP_ROM_PTR(&adafruit_controller_combined_product_id_and_firmware_date_code_object)},
};
static MP_DEFINE_CONST_DICT(adafruit_controller_locals, adafruit_controller_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(iot_adafruit_controller_type, MP_QSTR_AdafruitMiniI2cGamepad, MP_TYPE_FLAG_NONE, make_new,
                         adafruit_controller_make_new, locals_dict, &adafruit_controller_locals);

// Shared operations only; calibration and firmware details belong to Adafruit.
static const mp_rom_map_elem_t controller_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&controller_close_object)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&controller_close_object)},
    {MP_ROM_QSTR(MP_QSTR_model_name), MP_ROM_PTR(&controller_model_name_object)},
    {MP_ROM_QSTR(MP_QSTR_board_type), MP_ROM_PTR(&controller_board_type_object)},
    {MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&controller_connect_object)},
    {MP_ROM_QSTR(MP_QSTR_refresh_input_state), MP_ROM_PTR(&controller_refresh_input_state_object)},
    {MP_ROM_QSTR(MP_QSTR_is_connected), MP_ROM_PTR(&controller_is_connected_object)},
    {MP_ROM_QSTR(MP_QSTR_joystick), MP_ROM_PTR(&controller_joystick_object)},
    {MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&controller_buttons_object)},
};
static MP_DEFINE_CONST_DICT(controller_locals, controller_locals_table);
MP_DEFINE_CONST_OBJ_TYPE(iot_seengreat_controller_type, MP_QSTR_SeenGreatOledHatController, MP_TYPE_FLAG_NONE, make_new,
                         seengreat_controller_make_new, locals_dict, &controller_locals);

static const mp_rom_map_elem_t input_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_input)},
    {MP_ROM_QSTR(MP_QSTR_AdafruitMiniI2cGamepad), MP_ROM_PTR(&iot_adafruit_controller_type)},
    {MP_ROM_QSTR(MP_QSTR_SeenGreatOledHatController), MP_ROM_PTR(&iot_seengreat_controller_type)},
    {MP_ROM_QSTR(MP_QSTR_ControllerJoystick), MP_ROM_PTR(&iot_controller_joystick_type)},
    {MP_ROM_QSTR(MP_QSTR_ControllerButtons), MP_ROM_PTR(&iot_controller_buttons_type)},
};
static MP_DEFINE_CONST_DICT(input_module_globals, input_module_globals_table);

const mp_obj_module_t iot_private_input_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&input_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_input, iot_private_input_module);
