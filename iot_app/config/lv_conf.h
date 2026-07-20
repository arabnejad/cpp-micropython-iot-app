#ifndef IOT_LV_CONF_H
#define IOT_LV_CONF_H

#define LV_CONF_H

/* Project-owned LVGL configuration. Keep upstream lvgl/ unmodified. */

#define LV_COLOR_DEPTH 32
#define LV_USE_OS LV_OS_NONE

/*
 * LVGL allocates memory for widgets and drawing buffers. Its built-in fixed
 * heap may be too small for the drawing buffer required by a high-resolution
 * framebuffer. Use standard C allocation so LVGL can request memory from the
 * application's process heap.
 */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_LINUX_DRM 0

/* Draw through the console framebuffer without changing its display mode. */
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_BSD 0
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_LINUX_FBDEV_BUFFER_COUNT 1
#define LV_LINUX_FBDEV_BUFFER_SIZE 0
#define LV_LINUX_FBDEV_MMAP 1

/* IoT App decodes JPEG files before sending pixels to LVGL. */
#define LV_USE_FS_POSIX 0
#define LV_USE_TJPGD 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_GIF 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_24

#define LV_USE_FREETYPE 0
#define LV_USE_SDL 0
#define LV_USE_WAYLAND 0
#define LV_USE_X11 0

#endif /* IOT_LV_CONF_H */
