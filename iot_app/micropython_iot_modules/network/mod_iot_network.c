#include "network_cpp_bridge.h"

#include "py/objstr.h"
#include "py/runtime.h"

#include <string.h>

static void raise_native_error(iot_native_result_t nativeCallResult) {
  if (!nativeCallResult.succeeded) {
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), nativeCallResult.error_message);
  }
}

static void store_download_value(mp_obj_t dictionary, qstr key, mp_obj_t value) {
  mp_obj_dict_store(dictionary, MP_OBJ_NEW_QSTR(key), value);
}

static void store_download_string(mp_obj_t dictionary, qstr key, const char *value) {
  store_download_value(dictionary, key, mp_obj_new_str(value, strlen(value)));
}

static mp_obj_t network_download_file(size_t number_of_arguments, const mp_obj_t *positional_arguments,
                                      mp_map_t *keyword_arguments) {
  /* Gives a readable name to each position in the parsed argument array. */
  enum { ARG_url, ARG_expected_sha256 };
  static const mp_arg_t allowed_arguments[] = {
      {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_expected_sha256, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
  };
  mp_arg_val_t arguments[MP_ARRAY_SIZE(allowed_arguments)];
  mp_arg_parse_all(number_of_arguments, positional_arguments, keyword_arguments, MP_ARRAY_SIZE(allowed_arguments),
                   allowed_arguments, arguments);

  const char *url            = mp_obj_str_get_str(arguments[ARG_url].u_obj);
  const char *expectedSha256 = arguments[ARG_expected_sha256].u_obj == MP_OBJ_NULL
                                   ? ""
                                   : mp_obj_str_get_str(arguments[ARG_expected_sha256].u_obj);

  iot_downloaded_file_t downloadedFile = {0};
  raise_native_error(iot_network_download_file(url, expectedSha256, &downloadedFile));

  mp_obj_t downloadedFileDictionary = mp_obj_new_dict(5);
  store_download_string(downloadedFileDictionary, MP_QSTR_path, downloadedFile.file_path);
  store_download_string(downloadedFileDictionary, MP_QSTR_sha256, downloadedFile.sha256);
  store_download_value(downloadedFileDictionary, MP_QSTR_size_bytes,
                       mp_obj_new_int_from_ull(downloadedFile.size_in_bytes));
  store_download_string(downloadedFileDictionary, MP_QSTR_content_type, downloadedFile.content_type);
  store_download_value(downloadedFileDictionary, MP_QSTR_loaded_from_cache,
                       mp_obj_new_bool(downloadedFile.loaded_from_cache));
  return downloadedFileDictionary;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(network_download_file_object, 0, network_download_file);

static const mp_rom_map_elem_t network_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__iot_network)},
    {MP_ROM_QSTR(MP_QSTR_download_file), MP_ROM_PTR(&network_download_file_object)},
};
static MP_DEFINE_CONST_DICT(network_module_globals, network_module_globals_table);

const mp_obj_module_t iot_private_network_module = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&network_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__iot_network, iot_private_network_module);
