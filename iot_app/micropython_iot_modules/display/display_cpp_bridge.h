#pragma once

#include "iot_native_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One display mode copied from C++ into plain C fields. */
typedef struct {
  const char *name;
  uint32_t    width;
  uint32_t    height;
  uint32_t    refresh_rate_hz;
  int         preferred;
  int         interlaced;
} iot_display_mode_information_t;

/* Details for one monitor found during IoT App startup. */
typedef struct {
  const char                    *connector_name;
  const char                    *manufacturer;
  const char                    *model;
  const char                    *serial_number;
  uint32_t                       physical_width_mm;
  uint32_t                       physical_height_mm;
  int                            active;
  int                            has_current_mode;
  iot_display_mode_information_t current_mode;
  size_t                         supported_mode_count;
} iot_monitor_information_t;

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

typedef enum {
  IOT_BACKGROUND_IMAGE_CENTER = 0,
  IOT_BACKGROUND_IMAGE_FIT,
  IOT_BACKGROUND_IMAGE_TILE,
} iot_background_image_mode_t;

iot_native_result_t iot_display_clear(uint8_t red, uint8_t green, uint8_t blue);

iot_native_result_t iot_display_draw_text_box(int32_t x, int32_t y, int32_t width, int32_t height, const char *text,
                                              const iot_text_box_options_t *text_box_options, uint64_t *widget_id);

/* Changes text in a box previously created by Python. */
iot_native_result_t iot_display_update_text_box(uint64_t widget_id, const char *text);

/* Moves a box previously created by Python to an absolute screen position. */
iot_native_result_t iot_display_move_text_box(uint64_t widget_id, int32_t x, int32_t y);

/* Deletes a box previously created by Python. */
iot_native_result_t iot_display_delete_text_box(uint64_t widget_id);

/* Draws a JPEG and returns the widget ID used for later changes. */
iot_native_result_t iot_display_draw_image(const char *file_path, int32_t x, int32_t y, uint16_t scale_percent,
                                           uint64_t *widget_id);
/* Changes the JPEG used by an existing image widget. */
iot_native_result_t iot_display_update_image(uint64_t widget_id, const char *file_path);
/* Moves an existing image widget. */
iot_native_result_t iot_display_move_image(uint64_t widget_id, int32_t x, int32_t y);
/* Redecodes an image at a smaller or original size. */
iot_native_result_t iot_display_set_image_scale(uint64_t widget_id, uint16_t scale_percent);
/* Deletes an image widget. */
iot_native_result_t iot_display_delete_image(uint64_t widget_id);
/* Places a JPEG behind all normal widgets. */
iot_native_result_t iot_display_set_background_image(const char *file_path, iot_background_image_mode_t mode,
                                                     uint16_t scale_percent);
/* Removes the background JPEG without changing the screen colour. */
iot_native_result_t iot_display_clear_background_image(void);

iot_native_result_t iot_display_fill_area(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t red,
                                          uint8_t green, uint8_t blue);

iot_native_result_t iot_display_size(uint32_t *width, uint32_t *height);
iot_native_result_t iot_display_monitor_count(size_t *monitor_count);
iot_native_result_t iot_display_monitor_information(size_t                     monitor_index,
                                                    iot_monitor_information_t *monitor_information);
iot_native_result_t iot_display_supported_mode_information(size_t monitor_index, size_t mode_index,
                                                           iot_display_mode_information_t *mode_information);

#ifdef __cplusplus
}
#endif
