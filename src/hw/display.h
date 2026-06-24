// ST7701 480x480 RGB-parallel panel + LVGL 9 init.
// ⚠️ Phase-0 GATE: sustained flicker-free redraw WHILE WiFi is active (plan §2).
// Pins/timings verified from Elecrow's RotaryScreen_2_1.ino — see board_pins.h.
#pragma once

#include <stdint.h>

bool displayInit();        // power panel, init ST7701 + LVGL; false if panel alloc fails
void backlightSet(uint8_t pct);

// Bring-up helper: draw solid R/G/B/W quadrants straight to the panel (bypasses LVGL) to
// confirm the RGB data pin map + color order. Wrong pins => wrong/garbled colors.
void displayTestPattern();
