#pragma once

#include "iot_native_result.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Display values copied from C++ into plain C fields. */
typedef struct {
  size_t      connected_display_count;
  const char *connector_name;
  const char *manufacturer;
  const char *model;
  uint32_t    width;
  uint32_t    height;
  uint32_t    refresh_rate_hz;
} iot_display_information_t;

/*
 * Optional text-box values supplied by Python.
 *
 * A value of -1 means Python did not provide the argument. C++ then keeps the
 * TextBoxSpec default.
 */
typedef struct {
  int     provided;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} iot_optional_color_t;

typedef struct {
  iot_optional_color_t text_color;
  iot_optional_color_t background_color;
  iot_optional_color_t border_color;
  int32_t              background_opacity;
  int32_t              border_width;
  int32_t              font_size;
} iot_text_box_options_t;

iot_native_result_t iot_display_clear(uint8_t red, uint8_t green, uint8_t blue);

iot_native_result_t iot_display_draw_text_box(int32_t x, int32_t y, int32_t width, int32_t height, const char *text,
                                              const iot_text_box_options_t *text_box_options, uint64_t *widget_id);

/* Changes text in a box previously created by Python. */
iot_native_result_t iot_display_update_text_box(uint64_t widget_id, const char *text);

/* Moves a box previously created by Python to an absolute screen position. */
iot_native_result_t iot_display_move_text_box(uint64_t widget_id, int32_t x, int32_t y);

/* Deletes a box previously created by Python. */
iot_native_result_t iot_display_delete_text_box(uint64_t widget_id);

iot_native_result_t iot_display_fill_area(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t red,
                                          uint8_t green, uint8_t blue);

iot_native_result_t iot_display_size(uint32_t *width, uint32_t *height);
iot_native_result_t iot_display_information(iot_display_information_t *display_information);

#ifdef __cplusplus
}
#endif
