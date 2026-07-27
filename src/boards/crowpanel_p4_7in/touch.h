// GT911 capacitive touch -> LVGL pointer input for the CrowPanel Advance 7".
// Touch is push-based from the UI's point of view: this registers an LVGL indev, so a unit never
// polls it directly (see core/board.h).
#pragma once

// Reset the controller into a known I2C address and register the LVGL indev.
// Call after displayInit() (LVGL must exist) and after Wire.begin(). False if the GT911 does not
// answer on the bus.
bool touchInit();
