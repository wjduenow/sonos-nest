// LVGL 9.x config for sonos-nest (ST7701 480x480, RGB565).
// Only overrides are set here; everything else falls back to lv_conf_internal.h defaults.
// Resolved via -DLV_CONF_INCLUDE_SIMPLE + -I include (see platformio.ini).
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// --- Color: RGB565 to match the panel framebuffer ---
#define LV_COLOR_DEPTH 16

// --- Memory: builtin allocator. Bump LV_MEM_SIZE as the UI (lists, image cache) grows. ---
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64U * 1024U)

// --- Tick: provided at runtime via lv_tick_set_cb(millis) in displayInit(). ---

// --- Logging (handy during bring-up; quiet in normal builds) ---
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// --- Fonts: 14 (default) for lists; 28 for now-playing title later ---
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1

// --- Default theme ---
#define LV_USE_THEME_DEFAULT 1

#endif // LV_CONF_H
