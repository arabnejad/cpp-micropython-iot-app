#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Success flag and error text returned from C++ to MicroPython C code. */
typedef struct {
  int         succeeded;
  const char *error_message;
} iot_native_result_t;

/* Native object or error text returned when Python creates a C++ object. */
typedef struct {
  void       *value;
  const char *error_message;
} iot_native_pointer_result_t;

/* Joystick and button values copied from either controller into plain C fields. */
typedef struct {
  int      x;
  int      y;
  int      centre_x;
  int      centre_y;
  int      dead_zone;
  uint32_t pressed_buttons_mask;
} iot_controller_state_t;

/*
 * Adafruit's Linux I2C connection. The native controller owns device_path;
 * the MicroPython module copies it into a Python string before returning.
 */
typedef struct {
  int         bus_number;
  uint8_t     address;
  const char *device_path;
} iot_adafruit_controller_connection_information_t;

/* Hardware and firmware information reported by the Adafruit gamepad. */
typedef struct {
  uint8_t  processor_hardware_id;
  uint32_t combined_product_id_and_firmware_date_code;
  uint16_t firmware_product_id;
  uint16_t firmware_date_code;
} iot_adafruit_controller_device_information_t;

#ifdef __cplusplus
}
#endif
