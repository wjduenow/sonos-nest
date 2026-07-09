// Board pin map — Hosyond / LCDWIKI ES3C28P 2.8" ESP32-S3 display board.
//
// GPIO assignments VERIFIED (2026-07-09) by cross-checking two independent sources that
// agree on every pin — the same rigor the nest board's pins got from Elecrow's example:
//   1. BSP header:  github.com/ngttai/esp32_s3_es3c28p
//                   include/bsp/esp32_s3_es3c28p.h  (the BSP_* #defines below, verbatim)
//   2. ESPHome cfg: github.com/celer/esphome_esp32-s3-2.8-display
//                   skeleton-config/es3c28p.yaml    (independent confirmation)
// Authoritative datasheet: LCDWIKI ES3C28P/ES3N28P User Manual (lcdwiki.com/2.8inch_ESP32-S3_Display).
// Mechanical spec: hardware/rec-2.8/ES3C28P_Size.pdf + hardware/rec-2.8/README.md.
//
// Board facts (from the datasheet):
//   - MCU:     ESP32-S3R8 (8 MB OPI PSRAM, 16 MB flash) — same class as the nest board
//   - Display: ILI9341V, 4-wire SPI, 240x320  (needs COLOR INVERSION on — see below)
//   - Touch:   FT6336G, I2C @ 0x38  (shares the I2C bus with the ES8311 audio codec @ 0x18)
//   - Audio:   ES8311 codec (speaker) + MEMS mic, both on I2S
//   - Extras:  microSD (SDIO 4-bit), addressable WS2812 RGB LED on IO42, battery ADC
#pragma once

// --- Panel geometry (portrait NATIVE; the driver rotates to landscape) ---
#define LCD_WIDTH   240
#define LCD_HEIGHT  320

// Display rotation (Arduino_GFX convention). The sleep-machine unit mounts LANDSCAPE in its
// nightstand stand (86mm wide x 50mm tall — see hardware/rec-2.8/countertop/), so the panel
// runs rotated: logical resolution is 320x240. Both display.cpp and touch.cpp key off this
// so the touch axes track the image. Flip 1<->3 if the image/stand ends up upside down.
#define DISPLAY_ROTATION  3
// ILI9341V on this board is wired for inverted colors: the display init must enable
// color inversion (Arduino_GFX: pass `true` for IPS/invert; ESPHome uses invert_colors:true).
// If reds look cyan / the image is a photonegative, this is the flag to flip.
#define LCD_INVERT_COLORS  1

// --- Display: ILI9341V over 4-wire SPI ---
#define PIN_LCD_MOSI   11
#define PIN_LCD_MISO   13   // present but unused by the display (write-only); shared with SD D0
#define PIN_LCD_SCLK   12
#define PIN_LCD_CS     10
#define PIN_LCD_DC     46
// LCD reset is NOT a dedicated GPIO — it shares the ESP32-S3 chip reset. The panel
// constructor must be told RST = -1 / GFX_NOT_DEFINED (no per-panel reset pulse).
#define PIN_LCD_RST    (-1)

// --- Backlight: plain active-high GPIO (drive HIGH = on). Can be LEDC-PWM'd for dimming. ---
#define PIN_BACKLIGHT  45

// --- Shared I2C bus (FT6336 touch + ES8311 audio codec) ---
#define PIN_I2C_SDA    16
#define PIN_I2C_SCL    15

// --- Touch: FT6336G capacitive controller ---
#define TOUCH_ADDR     0x38   // FT63x6 family
#define PIN_TOUCH_INT  17
#define PIN_TOUCH_RST  18

// --- Audio (for the sleep-machine's ambient sounds / wake-to-music later) ---
// ES8311 codec on I2C @ 0x18; audio samples over I2S.
#define I2S_ADDR_ES8311  0x18
#define PIN_I2S_MCLK   4
#define PIN_I2S_BCLK   5    // aka I2S SCLK
#define PIN_I2S_LRCLK  7    // aka I2S WS / LCLK
#define PIN_I2S_DOUT   8    // -> speaker (codec)
#define PIN_I2S_DIN    6    // <- MEMS mic
#define PIN_SPK_ENABLE 1    // speaker power-amp enable (ACTIVE-LOW / inverted)

// --- microSD — SDIO 4-bit mode (NOT SPI on this board; see conflict note) ---
#define PIN_SD_CLK     38
#define PIN_SD_CMD     40
#define PIN_SD_D0      39
#define PIN_SD_D1      41
#define PIN_SD_D2      48
#define PIN_SD_D3      47
// CONFLICT — do not wire SD as SPI here: the BSP also defines an SD-over-SPI variant with
// CS=GPIO42, but GPIO42 is the WS2812 RGB LED on this board (confirmed by ESPHome + our
// CLAUDE.md board notes). SD must use SDIO mode above; the BSP's SD-SPI block is a generic
// alternate that does not apply to the ES3C28P.

// --- Addressable RGB LED (single WS2812, GRB order) ---
#define PIN_RGB_LED    42

// --- Battery voltage sense (ADC, through a /2 divider) ---
#define PIN_BATTERY_ADC  9
