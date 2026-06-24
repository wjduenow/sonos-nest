// ST7701 480x480 RGB-parallel panel + LVGL init.
// ⚠️ Phase-0 GATE: sustained flicker-free redraw WHILE WiFi is active (plan §2).
// Pull exact ST7701 timings + RGB data pin map from Elecrow's board config.
#pragma once

void displayInit();   // init RGB panel, framebuffer in PSRAM, LVGL display driver
void backlightSet(uint8_t pct);
