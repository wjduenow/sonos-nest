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
#ifdef UNIT_JUKEBOX
// The jukebox draws four full-screen 1024x600 pages plus a keyboard, then lists favourites into
// whatever is left — 96 KB is the 480x480 nest's budget and it does not stretch that far. It is
// why a 70-row Favourites list froze the device (LVGL answers pool exhaustion with a layer-alloc
// RETRY LOOP, not a failure) and why the row cap in screens.cpp exists at all.
//
// The pool goes in PSRAM, not internal RAM. With LV_MEM_ADR 0 and no allocator, LVGL declares a
// static LV_MEM_SIZE array — internal SRAM, which this board idles at only ~70-100 KB free. Via
// LV_MEM_POOL_ALLOC it calls us instead (lv_mem_core_builtin.c), so 512 KB costs 1.6% of the 32 MB
// PSRAM and nothing internal. The frame buffer is already PSRAM and renders DIRECT into it, so the
// pool living there is consistent with how this panel already draws.
//   NB: these are macro DEFINITIONS only — lv_conf.h is also fed to LVGL's assembler, so it must
//   not #include anything. LVGL does the #include itself, in the C file that needs it.
#define LV_MEM_SIZE (512U * 1024U)
#define LV_MEM_POOL_INCLUDE     "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
#else
#define LV_MEM_SIZE (96U * 1024U)   // pool for LVGL objects + draw layers (clock scale,
                                    // browse lists). 64K overflowed: queue buttons + the
                                    // scaled clock's ~35K layer didn't both fit.
#endif

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

// sonos-jukebox only (1024x600). The design system specifies 52/22/15/13/10 px; these fill the
// gaps between the sizes above. Scoped to UNIT_JUKEBOX because every enabled face costs flash in
// EVERY unit's binary, and the S3 boards have no use for them.
#ifdef UNIT_JUKEBOX
#define LV_FONT_MONTSERRAT_12 1   // badges (design 10), timecodes (12)
#define LV_FONT_MONTSERRAT_16 1   // status bar room name (15)
#define LV_FONT_MONTSERRAT_22 1   // artist / album line (22)
#endif

// --- Default theme ---
#define LV_USE_THEME_DEFAULT 1

#endif // LV_CONF_H
