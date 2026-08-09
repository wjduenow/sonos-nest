// MIPI-DSI panel + LVGL display for the ELECROW CrowPanel Advance 7" ESP32-P4.
// EK79007, 1024x600, 2 DSI lanes @900 Mbps, 52 MHz DPI. See display.cpp for the two things that
// silently produce a "dead panel" if you get them wrong (the D-PHY LDO and the cache write-back).
#pragma once

#include <stdint.h>

// Bring up the D-PHY regulator, the DSI bus, the EK79007 and LVGL's display. Leaves the backlight
// OFF — call backlightSet() once there is something worth showing. False on any failure.
bool displayInit();

// The DPI frame buffer LVGL renders straight into (RGB565, LCD_WIDTH*LCD_HEIGHT*2 bytes).
// nullptr before displayInit() succeeds.
uint8_t *displayFrameBuffer();
