// Board pin map — CrowPanel 2.1" HMI ESP32-S3 Rotary Display (DHE03921D).
//
// ⚠️ VERIFY against Elecrow's schematic / example config before trusting these.
// The RGB data pins in particular must come from Elecrow's board config — do not
// hand-derive them. See plans/01-sonos-knob-controller-plan.md §2.
#pragma once

// --- Rotary encoder (direct GPIO) ---
#define PIN_ENCODER_A      42
#define PIN_ENCODER_B      4    // TODO: confirm not shared with an S3 strapping/JTAG fn

// --- I2C bus (touch + PCF8574 expander) ---
#define PIN_I2C_SDA        38
#define PIN_I2C_SCL        39

// --- Backlight ---
#define PIN_BACKLIGHT      6

// --- PCF8574 I2C expander @ 0x21 ---
// The knob PRESS is behind the expander (NOT a direct GPIO). Poll it / use its INT line.
#define PCF8574_ADDR       0x21
#define PCF_PIN_TOUCH_RST  0    // P0
#define PCF_PIN_TOUCH_INT  2    // P2
#define PCF_PIN_LCD_PWR    3    // P3
#define PCF_PIN_LCD_RST    4    // P4
#define PCF_PIN_KNOB_BTN   5    // P5  <-- play/pause button

// --- ST7701 RGB parallel control ---
#define PIN_RGB_DE         40
#define PIN_RGB_VSYNC      7
#define PIN_RGB_HSYNC      15
#define PIN_RGB_PCLK       41
// RGB data lines R0-R4 / G0-G5 / B0-B4: PULL FROM ELECROW CONFIG. Placeholder below.
// #define PIN_RGB_R0 ... etc.
