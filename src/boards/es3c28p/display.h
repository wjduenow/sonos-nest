// ILI9341V 240x320 panel over 4-wire SPI + LVGL 9 init (ES3C28P board).
// Pins verified in pins.h. Arduino_GFX pinned to 1.3.1 ships Arduino_ESP32SPI +
// Arduino_ILI9341, so no library bump is needed (see CLAUDE.md).
#pragma once

#include <stdint.h>

bool displayInit();        // init SPI bus + ILI9341 + LVGL; false if panel/buffer alloc fails
void backlightSet(uint8_t pct);

// Bring-up helper: draw solid R/G/B/W quadrants straight to the panel (bypasses LVGL) to
// confirm the SPI pin map, rotation, and color order/inversion before trusting LVGL.
void displayTestPattern();
