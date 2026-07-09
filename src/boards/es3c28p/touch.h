// FT6336G capacitive touch @ 0x38 (I2C), ES3C28P board. Registers an LVGL pointer input
// device so the UX never polls it directly. Minimal direct driver (no external lib), the
// FT6336 twin of the nest's CST816 touch.cpp.
#pragma once

#include <stdint.h>

void touchInit();
// Returns true if a finger is down; writes panel coords. Used by the LVGL read callback
// and by the bring-up self-test.
bool touchRead(uint16_t &x, uint16_t &y);
