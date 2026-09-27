#pragma once

#include "iot_native_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

iot_native_pointer_result_t iot_adafruit_controller_create(void);
iot_native_pointer_result_t iot_seengreat_controller_create(void);
void                        iot_controller_destroy(void *controller_handle);

iot_native_result_t iot_controller_model_name(void *controller_handle, const char **model_name);
iot_native_result_t iot_controller_board_type(void *controller_handle, const char **board_type);
iot_native_result_t iot_controller_connect(void *controller_handle);
iot_native_result_t iot_adafruit_controller_calibrate_joystick(void *controller_handle, size_t number_of_samples,
                                                               int dead_zone);
iot_native_result_t iot_controller_refresh_input_state(void *controller_handle);
iot_native_result_t iot_controller_is_connected(void *controller_handle, int *is_connected);
iot_native_result_t iot_controller_read_state(void *controller_handle, iot_controller_state_t *state);
iot_native_result_t iot_controller_joystick_direction(void *controller_handle, const char **direction);
iot_native_result_t iot_adafruit_controller_read_connection_information(
    void *controller_handle, iot_adafruit_controller_connection_information_t *connection_information);
iot_native_result_t iot_adafruit_controller_read_diagnostics(void *controller_handle,
                                                             iot_adafruit_controller_device_information_t *diagnostics);

#ifdef __cplusplus
}
#endif
