#ifndef IOT_LV_CONF_H
#define IOT_LV_CONF_H

#define LV_CONF_H

/* Project-owned LVGL configuration. Keep upstream lvgl/ unmodified. */

#define LV_COLOR_DEPTH 32
#define LV_USE_OS LV_OS_NONE

/*
 * LVGL uses this private heap for widgets and framebuffer drawing buffers.
 * The upstream 64 KiB default is too small for a 1920-pixel-wide display.
 */
#define LV_MEM_SIZE (512U * 1024U)

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_LINUX_DRM 0

/* Draw through the console framebuffer without changing its display mode. */
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_BSD 0
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 1
/* Render 20 screen rows at a time instead of allocating a full-screen buffer. */
#define LV_LINUX_FBDEV_BUFFER_SIZE 20
#define LV_LINUX_FBDEV_MMAP 1

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
