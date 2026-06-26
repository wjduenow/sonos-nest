// LVGL 9.x config for sonos-nest (ST7701 480x480, RGB565).
// Only overrides are set here; everything else falls back to lv_conf_internal.h defaults.
// Resolved via -DLV_CONF_INCLUDE_SIMPLE + -I include (see platformio.ini).
#ifndef LV_CONF_H
#define LV_CONF_H

// NOTE: lv_conf.h is also pulled in when LVGL's .S files are assembled, so it must be
// assembly-safe — do not #include C headers (e.g. <stdint.h>) here unguarded.

// --- Color: RGB565 to match the panel framebuffer ---
#define LV_COLOR_DEPTH 16

// --- We're on Xtensa (ESP32-S3): disable LVGL's ARM Helium/NEON assembly, otherwise its
//     .S files are fed to the Xtensa assembler and fail to build. ---
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

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
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1   // clock screensaver

// --- Default theme ---
#define LV_USE_THEME_DEFAULT 1

#endif // LV_CONF_H
