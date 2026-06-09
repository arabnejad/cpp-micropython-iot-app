#include "py/runtime.h"

extern const mp_obj_module_t iot_private_display_module;
extern const mp_obj_module_t iot_private_input_module;
extern const mp_obj_module_t iot_private_scheduler_module;
extern const mp_obj_module_t iot_private_system_module;

/*
 * Python applications import iot and use iot.display, iot.input,
 * iot.scheduler, and iot.system. The table below connects those public names
 * to the built-in C modules that implement them.
 *
 * Applications do not need to know the internal module names. This also lets
 * the public iot API add Python helper functions later without changing the
 * low-level C++ drivers.
 */
static const mp_rom_map_elem_t iot_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_iot)},
    {MP_ROM_QSTR(MP_QSTR_display), MP_ROM_PTR(&iot_private_display_module)},
    {MP_ROM_QSTR(MP_QSTR_input), MP_ROM_PTR(&iot_private_input_module)},
    {MP_ROM_QSTR(MP_QSTR_scheduler), MP_ROM_PTR(&iot_private_scheduler_module)},
    {MP_ROM_QSTR(MP_QSTR_system), MP_ROM_PTR(&iot_private_system_module)},
};
static MP_DEFINE_CONST_DICT(iot_module_globals, iot_module_globals_table);

const mp_obj_module_t iot_public_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&iot_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_iot, iot_public_module);
