// Board pin map — CrowPanel 2.1" HMI ESP32-S3 Rotary Display (DHE03921D).
//
// Values verified against Elecrow's official example source:
//   github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen
//   -> example/RotaryScreen_2_1/RotaryScreen_2_1.ino  (constructor args taken verbatim)
// Cross-checked against the Elecrow wiki. See plans/01-sonos-knob-controller-plan.md §2/§8.
#pragma once

// --- Rotary encoder (direct GPIO) ---
#define PIN_ENCODER_A      42
#define PIN_ENCODER_B      4
#define PIN_ENCODER_LED    43   // encoder "breathe" LED (LEDC); optional

// --- I2C bus (CST816 touch + PCF8574 expander share it) ---
#define PIN_I2C_SDA        38
#define PIN_I2C_SCL        39

// --- Backlight (LEDC PWM) ---
#define PIN_BACKLIGHT      6

// --- PCF8574 I2C expander @ 0x21 ---
// The knob PRESS is behind the expander (NOT a direct GPIO). Poll it / use its INT line.
#define PCF8574_ADDR       0x21
#define PCF_PIN_TOUCH_RST  0    // P0
#define PCF_PIN_TOUCH_INT  2    // P2
#define PCF_PIN_LCD_PWR    3    // P3
#define PCF_PIN_LCD_RST    4    // P4
#define PCF_PIN_KNOB_BTN   5    // P5  <-- play/pause button (active-low, pull-up)

// --- CST816 capacitive touch ---
#define TOUCH_ADDR         0x15

// --- ST7701 3-wire SPI config bus (panel register init only; pixels go over RGB below) ---
#define PIN_LCD_CS         16
#define PIN_LCD_SCK        2
#define PIN_LCD_SDA        1    // MOSI for the SPI init line
// LCD reset is NOT a direct GPIO — it's PCF8574 P4 (pulse via pcfLcdReset()); the panel
// constructor is told RST = GFX_NOT_DEFINED.

// --- ST7701 RGB parallel control ---
#define PIN_RGB_DE         40
#define PIN_RGB_VSYNC      7
#define PIN_RGB_HSYNC      15
#define PIN_RGB_PCLK       41

// --- ST7701 RGB565 data lines (R0-R4, G0-G5, B0-B4) ---
#define PIN_RGB_R0         46
#define PIN_RGB_R1         3
#define PIN_RGB_R2         8
#define PIN_RGB_R3         18
#define PIN_RGB_R4         17
#define PIN_RGB_G0         14
#define PIN_RGB_G1         13
#define PIN_RGB_G2         12
#define PIN_RGB_G3         11
#define PIN_RGB_G4         10
#define PIN_RGB_G5         9
#define PIN_RGB_B0         5
#define PIN_RGB_B1         45
#define PIN_RGB_B2         48
#define PIN_RGB_B3         47
#define PIN_RGB_B4         21

// --- ST7701 panel geometry + timings (Elecrow source literals) ---
#define LCD_WIDTH          480
#define LCD_HEIGHT         480
#define LCD_HSYNC_FP       10
#define LCD_HSYNC_PW       4    // wiki/comments say 8; source literal is 4 — trust source
#define LCD_HSYNC_BP       20
#define LCD_VSYNC_FP       10
#define LCD_VSYNC_PW       4
#define LCD_VSYNC_BP       20
// prefer_speed (pixel clock) + polarity flags: Elecrow leaves these at Arduino_GFX
// defaults. If the panel tears/rolls, set prefer_speed explicitly (~12-16 MHz) and tune.
