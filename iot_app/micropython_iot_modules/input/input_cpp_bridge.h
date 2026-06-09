#pragma once

#include "iot_native_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

iot_native_pointer_result_t iot_gamepad_create(int i2c_bus_number, uint8_t i2c_address);
void                        iot_gamepad_destroy(void *gamepad_handle);

iot_native_result_t iot_gamepad_model_name(void *gamepad_handle, const char **model_name);
iot_native_result_t iot_gamepad_connect(void *gamepad_handle);
iot_native_result_t iot_gamepad_calibrate_joystick(void *gamepad_handle, size_t number_of_samples, int dead_zone);
iot_native_result_t iot_gamepad_refresh_input_state(void *gamepad_handle);
iot_native_result_t iot_gamepad_is_connected(void *gamepad_handle, int *is_connected);
iot_native_result_t iot_gamepad_read_state(void *gamepad_handle, iot_gamepad_state_t *state);
iot_native_result_t iot_gamepad_joystick_direction(void *gamepad_handle, const char **direction);
iot_native_result_t iot_gamepad_read_diagnostics(void *gamepad_handle, iot_gamepad_device_information_t *diagnostics);

#ifdef __cplusplus
}
#endif
