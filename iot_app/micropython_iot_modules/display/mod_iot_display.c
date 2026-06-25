#include "display_cpp_bridge.h"

#include "py/objstr.h"
#include "py/runtime.h"

#include <string.h>

static void raise_native_error(iot_native_result_t nativeCallResult) {
  if (!nativeCallResult.succeeded) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), nativeCallResult.error_message);
  }
}

static uint8_t color_component(mp_int_t colorComponentValue) {
  if (colorComponentValue < 0 || colorComponentValue > 255) {
    mp_raise_ValueError(MP_ERROR_TEXT("color components must be between 0 and 255"));
  }
  return (uint8_t)colorComponentValue;
}

static iot_optional_color_t optional_color(mp_obj_t pythonColor, const char *argumentName) {
  iot_optional_color_t color = {0};
  if (pythonColor == MP_OBJ_NULL) {
    return color;
  }

  size_t    numberOfComponents = 0U;
  mp_obj_t *components         = NULL;
  mp_obj_get_array(pythonColor, &numberOfComponents, &components);
  if (numberOfComponents != 3U) {
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s must contain red, green, and blue"), argumentName);
  }

  color.provided = 1;
  color.red      = color_component(mp_obj_get_int(components[0]));
  color.green    = color_component(mp_obj_get_int(components[1]));
  color.blue     = color_component(mp_obj_get_int(components[2]));
  return color;
}

static iot_optional_color_t color_or_default(mp_obj_t pythonColor, uint8_t red, uint8_t green, uint8_t blue,
                                             const char *argumentName) {
  iot_optional_color_t color = optional_color(pythonColor, argumentName);
  if (!color.provided) {
    color.provided = 1;
    color.red      = red;
    color.green    = green;
    color.blue     = blue;
  }
  return color;
}

static int32_t optional_integer(mp_obj_t optionalPythonInteger, const char *argumentName, mp_int_t minimum,
                                mp_int_t maximum) {
  if (optionalPythonInteger == MP_OBJ_NULL) {
    return -1;
  }

  const mp_int_t parsedIntegerValue = mp_obj_get_int(optionalPythonInteger);
  if (parsedIntegerValue < minimum || parsedIntegerValue > maximum) {
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s must be between %d and %d"), argumentName, (int)minimum,
                      (int)maximum);
  }
  return (int32_t)parsedIntegerValue;
}

static int32_t signed_32_bit_value(mp_int_t pythonIntegerValue) {
  if (pythonIntegerValue < INT32_MIN || pythonIntegerValue > INT32_MAX) {
    mp_raise_ValueError(MP_ERROR_TEXT("position and size values must fit in a signed 32-bit integer"));
  }
  return (int32_t)pythonIntegerValue;
}

static mp_obj_t display_clear(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                              mp_map_t *keyword_arguments) {
  enum { ARG_color };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all(number_of_arguments, positional_arguments, keyword_arguments, MP_ARRAY_SIZE(allowed_arguments),
                   allowed_arguments, arguments);

  const iot_optional_color_t color = color_or_default(arguments[ARG_color].u_obj, 0U, 0U, 0U, "color");
  raise_native_error(iot_display_clear(color.red, color.green, color.blue));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(display_clear_object, 0, display_clear);

static mp_obj_t display_draw_text_box(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                                      mp_map_t *keyword_arguments) {
  enum {
    ARG_x,
    ARG_y,
    ARG_width,
    ARG_height,
    ARG_text,
    ARG_text_color,
    ARG_background_color,
    ARG_border_color,
    ARG_background_opacity,
    ARG_border_width,
    ARG_font_size
  };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_width, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_text, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_text_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_background_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_border_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_background_opacity, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_border_width, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_font_size, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all(number_of_arguments, positional_arguments, keyword_arguments, MP_ARRAY_SIZE(allowed_arguments),
                   allowed_arguments, arguments);

  const char *text = mp_obj_str_get_str(arguments[ARG_text].u_obj);

  const iot_text_box_options_t textBoxOptions = {
      .text_color         = optional_color(arguments[ARG_text_color].u_obj, "text_color"),
      .background_color   = optional_color(arguments[ARG_background_color].u_obj, "background_color"),
      .border_color       = optional_color(arguments[ARG_border_color].u_obj, "border_color"),
      .background_opacity = optional_integer(arguments[ARG_background_opacity].u_obj, "background_opacity", 0, 255),
      .border_width       = optional_integer(arguments[ARG_border_width].u_obj, "border_width", 0, UINT16_MAX),
      .font_size          = optional_integer(arguments[ARG_font_size].u_obj, "font_size", 1, UINT16_MAX),
  };

  uint64_t widget_id = 0U;
  raise_native_error(iot_display_draw_text_box(
      signed_32_bit_value(arguments[ARG_x].u_int), signed_32_bit_value(arguments[ARG_y].u_int),
      signed_32_bit_value(arguments[ARG_width].u_int), signed_32_bit_value(arguments[ARG_height].u_int), text,
      &textBoxOptions, &widget_id));
  return mp_obj_new_int_from_ull(widget_id);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(display_draw_text_box_object, 0, display_draw_text_box);

static mp_obj_t display_update_text_box(mp_obj_t widget_id_object, mp_obj_t text_object) {
  const mp_int_t widget_id = mp_obj_get_int(widget_id_object);
  if (widget_id <= 0) {
    mp_raise_ValueError(MP_ERROR_TEXT("widget_id must be a positive integer"));
  }
  const char *text = mp_obj_str_get_str(text_object);
  raise_native_error(iot_display_update_text_box((uint64_t)widget_id, text));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(display_update_text_box_object, display_update_text_box);

static mp_obj_t display_move_text_box(mp_obj_t widget_id_object, mp_obj_t x_object, mp_obj_t y_object) {
  const mp_int_t widget_id = mp_obj_get_int(widget_id_object);
  if (widget_id <= 0) {
    mp_raise_ValueError(MP_ERROR_TEXT("widget_id must be a positive integer"));
  }

  raise_native_error(iot_display_move_text_box((uint64_t)widget_id, signed_32_bit_value(mp_obj_get_int(x_object)),
                                               signed_32_bit_value(mp_obj_get_int(y_object))));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(display_move_text_box_object, display_move_text_box);

static mp_obj_t display_delete_text_box(mp_obj_t widget_id_object) {
  const mp_int_t widget_id = mp_obj_get_int(widget_id_object);
  if (widget_id <= 0) {
    mp_raise_ValueError(MP_ERROR_TEXT("widget_id must be a positive integer"));
  }

  raise_native_error(iot_display_delete_text_box((uint64_t)widget_id));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_delete_text_box_object, display_delete_text_box);

static mp_obj_t display_fill_area(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                                  mp_map_t *keyword_arguments) {
  enum { ARG_x, ARG_y, ARG_width, ARG_height, ARG_color };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_width, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all(number_of_arguments, positional_arguments, keyword_arguments, MP_ARRAY_SIZE(allowed_arguments),
                   allowed_arguments, arguments);

  const iot_optional_color_t color = color_or_default(arguments[ARG_color].u_obj, 0U, 0U, 0U, "color");
  raise_native_error(
      iot_display_fill_area(signed_32_bit_value(arguments[ARG_x].u_int), signed_32_bit_value(arguments[ARG_y].u_int),
                            signed_32_bit_value(arguments[ARG_width].u_int),
                            signed_32_bit_value(arguments[ARG_height].u_int), color.red, color.green, color.blue));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(display_fill_area_object, 0, display_fill_area);

static mp_obj_t display_size(void) {
  uint32_t width  = 0;
  uint32_t height = 0;
  raise_native_error(iot_display_size(&width, &height));
  mp_obj_t size[] = {mp_obj_new_int_from_uint(width), mp_obj_new_int_from_uint(height)};
  return mp_obj_new_tuple(MP_ARRAY_SIZE(size), size);
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_size_object, display_size);

static void store_information_value(mp_obj_t dictionary, qstr key, mp_obj_t value) {
  mp_obj_dict_store(dictionary, MP_OBJ_NEW_QSTR(key), value);
}

static void store_information_string(mp_obj_t dictionary, qstr key, const char *value) {
  store_information_value(dictionary, key, mp_obj_new_str(value, strlen(value)));
}

static mp_obj_t display_mode_dictionary(const iot_display_mode_information_t *displayModeInformation) {
  mp_obj_t displayModeDictionary = mp_obj_new_dict(6);
  store_information_string(displayModeDictionary, MP_QSTR_name, displayModeInformation->name);
  store_information_value(displayModeDictionary, MP_QSTR_width,
                          mp_obj_new_int_from_uint(displayModeInformation->width));
  store_information_value(displayModeDictionary, MP_QSTR_height,
                          mp_obj_new_int_from_uint(displayModeInformation->height));
  store_information_value(displayModeDictionary, MP_QSTR_refresh_rate_hz,
                          mp_obj_new_int_from_uint(displayModeInformation->refresh_rate_hz));
  store_information_value(displayModeDictionary, MP_QSTR_preferred, mp_obj_new_bool(displayModeInformation->preferred));
  store_information_value(displayModeDictionary, MP_QSTR_interlaced,
                          mp_obj_new_bool(displayModeInformation->interlaced));
  return displayModeDictionary;
}

static mp_obj_t monitor_dictionary(size_t monitorIndex) {
  iot_monitor_information_t monitorInformation = {0};
  raise_native_error(iot_display_monitor_information(monitorIndex, &monitorInformation));

  mp_obj_t supportedModes = mp_obj_new_list(0, NULL);
  for (size_t modeIndex = 0; modeIndex < monitorInformation.supported_mode_count; ++modeIndex) {
    iot_display_mode_information_t modeInformation = {0};
    raise_native_error(iot_display_supported_mode_information(monitorIndex, modeIndex, &modeInformation));
    mp_obj_list_append(supportedModes, display_mode_dictionary(&modeInformation));
  }

  mp_obj_t monitorDictionary = mp_obj_new_dict(9);
  store_information_string(monitorDictionary, MP_QSTR_connector_name, monitorInformation.connector_name);
  store_information_string(monitorDictionary, MP_QSTR_manufacturer, monitorInformation.manufacturer);
  store_information_string(monitorDictionary, MP_QSTR_model, monitorInformation.model);
  store_information_string(monitorDictionary, MP_QSTR_serial_number, monitorInformation.serial_number);
  store_information_value(monitorDictionary, MP_QSTR_physical_width_mm,
                          mp_obj_new_int_from_uint(monitorInformation.physical_width_mm));
  store_information_value(monitorDictionary, MP_QSTR_physical_height_mm,
                          mp_obj_new_int_from_uint(monitorInformation.physical_height_mm));
  store_information_value(monitorDictionary, MP_QSTR_active, mp_obj_new_bool(monitorInformation.active));
  store_information_value(
      monitorDictionary, MP_QSTR_current_mode,
      monitorInformation.has_current_mode ? display_mode_dictionary(&monitorInformation.current_mode) : mp_const_none);
  store_information_value(monitorDictionary, MP_QSTR_supported_modes, supportedModes);
  return monitorDictionary;
}

static mp_obj_t display_monitors(void) {
  size_t monitorCount = 0U;
  raise_native_error(iot_display_monitor_count(&monitorCount));

  mp_obj_t monitors = mp_obj_new_list(0, NULL);
  for (size_t monitorIndex = 0; monitorIndex < monitorCount; ++monitorIndex) {
    mp_obj_list_append(monitors, monitor_dictionary(monitorIndex));
  }
  return monitors;
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_monitors_object, display_monitors);

static mp_obj_t display_active_monitor(void) {
  size_t monitorCount = 0U;
  raise_native_error(iot_display_monitor_count(&monitorCount));

  for (size_t monitorIndex = 0; monitorIndex < monitorCount; ++monitorIndex) {
    iot_monitor_information_t monitorInformation = {0};
    raise_native_error(iot_display_monitor_information(monitorIndex, &monitorInformation));
    if (monitorInformation.active) {
      return monitor_dictionary(monitorIndex);
    }
  }

  mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("active monitor is missing from the startup monitor list"));
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_active_monitor_object, display_active_monitor);

static const mp_rom_map_elem_t display_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_display)},
    {MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&display_clear_object)},
    {MP_ROM_QSTR(MP_QSTR_draw_text_box), MP_ROM_PTR(&display_draw_text_box_object)},
    {MP_ROM_QSTR(MP_QSTR_update_text_box), MP_ROM_PTR(&display_update_text_box_object)},
    {MP_ROM_QSTR(MP_QSTR_move_text_box), MP_ROM_PTR(&display_move_text_box_object)},
    {MP_ROM_QSTR(MP_QSTR_delete_text_box), MP_ROM_PTR(&display_delete_text_box_object)},
    {MP_ROM_QSTR(MP_QSTR_fill_area), MP_ROM_PTR(&display_fill_area_object)},
    {MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&display_size_object)},
    {MP_ROM_QSTR(MP_QSTR_monitors), MP_ROM_PTR(&display_monitors_object)},
    {MP_ROM_QSTR(MP_QSTR_active_monitor), MP_ROM_PTR(&display_active_monitor_object)},
};
static MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

const mp_obj_module_t iot_private_display_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&display_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_display, iot_private_display_module);
