#include "system_cpp_bridge.h"

#include "py/runtime.h"

#include <string.h>

static void raise_native_error(iot_native_result_t nativeCallResult) {
  if (!nativeCallResult.succeeded) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), nativeCallResult.error_message);
  }
}

static void store_value(mp_obj_t dictionary, qstr key, mp_obj_t value) {
  mp_obj_dict_store(dictionary, MP_OBJ_NEW_QSTR(key), value);
}

static void store_string(mp_obj_t dictionary, qstr key, const char *value) {
  store_value(dictionary, key, mp_obj_new_str(value, strlen(value)));
}

static void store_unsigned_integer(mp_obj_t dictionary, qstr key, uint64_t value) {
  store_value(dictionary, key, mp_obj_new_int_from_ull(value));
}

static iot_system_information_t read_system_information(void) {
  iot_system_information_t systemInformation = {0};
  raise_native_error(iot_system_read_information(&systemInformation));
  return systemInformation;
}

static mp_obj_t system_information(void) {
  const iot_system_information_t systemInformation           = read_system_information();
  mp_obj_t                       systemInformationDictionary = mp_obj_new_dict(6);
  store_string(systemInformationDictionary, MP_QSTR_hostname, systemInformation.hostname);
  store_string(systemInformationDictionary, MP_QSTR_device_model, systemInformation.device_model);
  store_string(systemInformationDictionary, MP_QSTR_operating_system, systemInformation.operating_system);
  store_string(systemInformationDictionary, MP_QSTR_kernel_version, systemInformation.kernel_version);
  store_string(systemInformationDictionary, MP_QSTR_architecture, systemInformation.architecture);
  store_unsigned_integer(systemInformationDictionary, MP_QSTR_uptime_seconds, systemInformation.uptime_seconds);
  return systemInformationDictionary;
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_information_object, system_information);

static mp_obj_t system_current_time(void) {
  const char *formatted_time = NULL;
  raise_native_error(iot_system_current_time(&formatted_time));
  return mp_obj_new_str(formatted_time, strlen(formatted_time));
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_current_time_object, system_current_time);

static mp_obj_t system_uptime_seconds(void) {
  uint64_t uptime_seconds = 0U;
  raise_native_error(iot_system_uptime_seconds(&uptime_seconds));
  return mp_obj_new_int_from_ull(uptime_seconds);
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_uptime_seconds_object, system_uptime_seconds);

static mp_obj_t system_resources(void) {
  const iot_system_information_t systemInformation             = read_system_information();
  mp_obj_t                       resourceInformationDictionary = mp_obj_new_dict(7);
  store_unsigned_integer(resourceInformationDictionary, MP_QSTR_logical_cpu_count, systemInformation.logical_cpu_count);
  store_value(resourceInformationDictionary, MP_QSTR_cpu_temperature_celsius,
              systemInformation.has_cpu_temperature ? mp_obj_new_float(systemInformation.cpu_temperature_celsius)
                                                    : mp_const_none);
  store_value(resourceInformationDictionary, MP_QSTR_one_minute_load_average,
              systemInformation.has_one_minute_load_average
                  ? mp_obj_new_float(systemInformation.one_minute_load_average)
                  : mp_const_none);
  store_unsigned_integer(resourceInformationDictionary, MP_QSTR_total_memory_bytes,
                         systemInformation.total_memory_bytes);
  store_unsigned_integer(resourceInformationDictionary, MP_QSTR_available_memory_bytes,
                         systemInformation.available_memory_bytes);
  store_unsigned_integer(resourceInformationDictionary, MP_QSTR_root_storage_total_bytes,
                         systemInformation.root_storage_total_bytes);
  store_unsigned_integer(resourceInformationDictionary, MP_QSTR_root_storage_available_bytes,
                         systemInformation.root_storage_available_bytes);
  return resourceInformationDictionary;
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_resources_object, system_resources);

static mp_obj_t system_network_interfaces(void) {
  size_t networkInterfaceCount = 0;
  raise_native_error(iot_system_network_interface_count(&networkInterfaceCount));

  mp_obj_t *networkInterfaceDictionaries = m_new(mp_obj_t, networkInterfaceCount);
  for (size_t index = 0; index < networkInterfaceCount; ++index) {
    iot_network_interface_information_t networkInterfaceInformation = {0};
    raise_native_error(iot_system_read_network_interface(index, &networkInterfaceInformation));
    mp_obj_t networkInterfaceDictionary = mp_obj_new_dict(4);
    store_string(networkInterfaceDictionary, MP_QSTR_name, networkInterfaceInformation.name);
    store_value(networkInterfaceDictionary, MP_QSTR_connected, mp_obj_new_bool(networkInterfaceInformation.connected));
    store_string(networkInterfaceDictionary, MP_QSTR_ipv4_address, networkInterfaceInformation.ipv4_address);
    store_value(networkInterfaceDictionary, MP_QSTR_speed_megabits_per_second,
                networkInterfaceInformation.has_speed
                    ? mp_obj_new_int_from_ull(networkInterfaceInformation.speed_megabits_per_second)
                    : mp_const_none);
    networkInterfaceDictionaries[index] = networkInterfaceDictionary;
  }
  return mp_obj_new_tuple(networkInterfaceCount, networkInterfaceDictionaries);
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_network_interfaces_object, system_network_interfaces);

static mp_obj_t system_interfaces(void) {
  const iot_system_information_t systemInformation               = read_system_information();
  mp_obj_t                       systemInterfaceCountsDictionary = mp_obj_new_dict(4);
  store_unsigned_integer(systemInterfaceCountsDictionary, MP_QSTR_i2c, systemInformation.i2c_interface_count);
  store_unsigned_integer(systemInterfaceCountsDictionary, MP_QSTR_gpio_controllers,
                         systemInformation.gpio_controller_count);
  store_unsigned_integer(systemInterfaceCountsDictionary, MP_QSTR_spi, systemInformation.spi_interface_count);
  store_unsigned_integer(systemInterfaceCountsDictionary, MP_QSTR_serial, systemInformation.serial_interface_count);
  return systemInterfaceCountsDictionary;
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_interfaces_object, system_interfaces);

static mp_obj_t system_devices(void) {
  const iot_system_information_t systemInformation               = read_system_information();
  mp_obj_t                       connectedDeviceCountsDictionary = mp_obj_new_dict(3);
  store_unsigned_integer(connectedDeviceCountsDictionary, MP_QSTR_usb, systemInformation.usb_device_count);
  store_unsigned_integer(connectedDeviceCountsDictionary, MP_QSTR_input, systemInformation.input_device_count);
  store_unsigned_integer(connectedDeviceCountsDictionary, MP_QSTR_block, systemInformation.block_device_count);
  return connectedDeviceCountsDictionary;
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_devices_object, system_devices);

static mp_obj_t system_app_information(void) {
  const iot_system_information_t systemInformation                = read_system_information();
  mp_obj_t                       applicationInformationDictionary = mp_obj_new_dict(4);
  store_string(applicationInformationDictionary, MP_QSTR_application_name, systemInformation.application_name);
  store_string(applicationInformationDictionary, MP_QSTR_app_version, systemInformation.app_version);
  store_string(applicationInformationDictionary, MP_QSTR_micropython_version, systemInformation.micropython_version);
  store_string(applicationInformationDictionary, MP_QSTR_lvgl_version, systemInformation.lvgl_version);
  return applicationInformationDictionary;
}
static MP_DEFINE_CONST_FUN_OBJ_0(system_app_information_object, system_app_information);

static const mp_rom_map_elem_t system_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_system)},
    {MP_ROM_QSTR(MP_QSTR_information), MP_ROM_PTR(&system_information_object)},
    {MP_ROM_QSTR(MP_QSTR_current_time), MP_ROM_PTR(&system_current_time_object)},
    {MP_ROM_QSTR(MP_QSTR_uptime_seconds), MP_ROM_PTR(&system_uptime_seconds_object)},
    {MP_ROM_QSTR(MP_QSTR_resources), MP_ROM_PTR(&system_resources_object)},
    {MP_ROM_QSTR(MP_QSTR_network_interfaces), MP_ROM_PTR(&system_network_interfaces_object)},
    {MP_ROM_QSTR(MP_QSTR_interfaces), MP_ROM_PTR(&system_interfaces_object)},
    {MP_ROM_QSTR(MP_QSTR_devices), MP_ROM_PTR(&system_devices_object)},
    {MP_ROM_QSTR(MP_QSTR_app_information), MP_ROM_PTR(&system_app_information_object)},
};
static MP_DEFINE_CONST_DICT(system_module_globals, system_module_globals_table);

const mp_obj_module_t iot_private_system_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&system_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_system, iot_private_system_module);
