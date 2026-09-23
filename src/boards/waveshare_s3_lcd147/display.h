// ST7789 panel driver for the Waveshare ESP32-S3-LCD-1.47B — the button-v3 "info screen".
//
// Deliberately NOT an LVGL display. This unit is HEADLESS (no core/ui/, no album art, the 3 s
// Sonos poll) and the panel exists to show one QR code and a handful of text lines a couple of
// times a day. Linking LVGL for that would cost a 96 KB LV_MEM_SIZE pool out of ~243 KB of
// internal heap — the tightest resource on the board — for a screen that is dark 99.9% of the
// time. See lib/qrcodegen/VENDORING.md for the same argument at more length.
//
// Everything here draws immediately and synchronously. There is no frame buffer, no flush
// callback and no task: infoScreenShow() is called from uiProvisioning() before any UI task
// exists, so it cannot depend on one.
#pragma once

#include <stdint.h>

// Bring up SPI + the ST7789 and leave the panel BLANK with the backlight off. Returns false if
// the panel could not be constructed. The unit lights it when it has something to say.
bool displayInit();

// Backlight 0..100% (LEDC PWM on PIN_LCD_BL).
void displayBacklight(uint8_t pct);

// Paint the whole page: a QR code on the left, caption + status lines on the right.
// qrText  — encoded at ECC M; if it will not fit QR_MAX_VER an error panel is drawn instead.
// caption — one short line telling the reader what scanning it will do.
// lines   — nLines status lines; may be nullptr/0.
// Does NOT touch the backlight; the caller decides when the page becomes visible.
void displayQrPage(const char *qrText, const char *caption,
                   const char *const *lines, uint8_t nLines);

// Backlight to 0 and the framebuffer to black.
//
// Deliberately NOT the ST7789's own sleep-in. Arduino_GFX's displayOff()/displayOn() issue
// SLPIN/SLPOUT and then block for ST7789_SLPIN_DELAY / SLPOUT_DELAY — 120 ms each. This panel is
// woken by a BUTTON PRESS, and the press classifier is polled from uiTick at ~5 ms against a 30 ms
// debounce window (boards/button_common/button.cpp), so a 120 ms stall lands exactly on the finger
// that caused it and can eat the press. The board is permanently USB-powered and the panel
// controller's idle draw is a rounding error against the Wi-Fi radio, so there is nothing to buy.
void displayBlank();
