#pragma once

#include "iot_native_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System values copied from C++ into plain C fields. */
typedef struct {
  const char *hostname;
  const char *device_model;
  const char *operating_system;
  const char *kernel_version;
  const char *architecture;

  uint64_t uptime_seconds;
  uint32_t logical_cpu_count;
  int      has_cpu_temperature;
  double   cpu_temperature_celsius;
  int      has_one_minute_load_average;
  double   one_minute_load_average;
  uint64_t total_memory_bytes;
  uint64_t available_memory_bytes;
  uint64_t root_storage_total_bytes;
  uint64_t root_storage_available_bytes;

  size_t i2c_interface_count;
  size_t gpio_controller_count;
  size_t spi_interface_count;
  size_t serial_interface_count;
  size_t usb_device_count;
  size_t input_device_count;
  size_t block_device_count;

  const char *application_name;
  const char *app_version;
  const char *micropython_version;
  const char *lvgl_version;
} iot_system_information_t;

/* One network interface copied from C++ into plain C fields. */
typedef struct {
  const char *name;
  int         connected;
  const char *ipv4_address;
  int         has_speed;
  uint64_t    speed_megabits_per_second;
} iot_network_interface_information_t;

/* Copies the startup snapshot into system_information. */
iot_native_result_t iot_system_read_information(iot_system_information_t *system_information);
/* Reads local time as YYYY-MM-DD HH:MM:SS. */
iot_native_result_t iot_system_current_time(const char **formatted_time);
/* Reads the current Linux uptime without rebuilding all system information. */
iot_native_result_t iot_system_uptime_seconds(uint64_t *uptime_seconds);
/* Reads the current interfaces and returns the number that were found. */
iot_native_result_t iot_system_network_interface_count(size_t *count);
/* Copies one interface from the latest reading selected by its index. */
iot_native_result_t
iot_system_read_network_interface(size_t index, iot_network_interface_information_t *network_interface_information);

#ifdef __cplusplus
}
#endif
