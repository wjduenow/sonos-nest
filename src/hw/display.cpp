#include "display.h"
#include "../board_pins.h"
#include <Arduino.h>

// TODO Phase 0:
//  - Configure Arduino_GFX ST7701 RGB panel with Elecrow's exact timings + pin map.
//  - Allocate framebuffer(s) in PSRAM; tune bounce-buffer size to kill tearing.
//  - Register LVGL display + flush callback (consider direct vs double buffer).

void displayInit() {
  // TODO: panel + LVGL bring-up.
}

void backlightSet(uint8_t pct) {
  // TODO: PWM on PIN_BACKLIGHT (GPIO 6).
  (void)pct;
}
