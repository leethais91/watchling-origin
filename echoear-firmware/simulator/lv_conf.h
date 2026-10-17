/*
 * Host simulator LVGL config. Mirrors the firmware's sdkconfig LVGL settings that affect
 * rendering (RGB565, CLIB malloc/string/sprintf, built-in Montserrat sizes); everything
 * else falls back to LVGL defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1

#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH "SDL2/SDL.h"

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#endif /* LV_CONF_H */
