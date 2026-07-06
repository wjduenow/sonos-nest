// CST816 capacitive touch @ 0x15 (I2C). Reset via PCF8574 P0. See plan §2.
// Registers an LVGL pointer input device. Minimal direct driver (no external lib).
#pragma once

#include <stdint.h>

void touchInit();
// Returns true if a finger is down; writes panel coords. Used by the LVGL read callback
// and by the bring-up self-test.
bool touchRead(uint16_t &x, uint16_t &y);
