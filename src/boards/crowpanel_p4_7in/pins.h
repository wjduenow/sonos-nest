// Board pin map — ELECROW CrowPanel Advance 7" ESP32-P4 HMI AI Display (DHE04107D).
// 1024x600 IPS, MIPI-DSI, GT911 touch, dual speakers, PDM mic, swappable radio module.
//
// GPIO assignments taken from Elecrow's own `board_config.h`, as described pin-by-pin in
// their Arduino course PDF (docs/crowpanel-advance-p4-7in/Arduino_Lessons_*.pdf, lessons 01/09/
// audio/LoRa). Cross-check against the Eagle schematic in Elecrow's repo before trusting any
// pin for new hardware work:
//   github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen
// Product/wiki + manuals: docs/crowpanel-advance-p4-7in/README.md.
//
// Board facts:
//   - MCU:      ESP32-P4NRW32 — RISC-V dual-core HP @ 360 MHz (400 MHz on rev.300 parts),
//               single-core LP @ 40 MHz, 16 MB flash, 32 MB in-package PSRAM, 768 KB L2MEM.
//               *** NO RADIO ON THE MAIN SoC *** — see the wireless note below.
//   - Display:  1024x600 IPS over MIPI-DSI, EK79007 driver IC. Active area 155 x 87 mm.
//   - Touch:    GT911 capacitive, I2C, addr 0x5D (INT low at reset) or 0x14 (INT high).
//   - Audio:    NS4168 class-D amp -> TWO onboard speakers, I2S. Separate PDM mic.
//   - Wireless: ESP32-C6-MINI-1 module (Wi-Fi 6 + BLE 5.3) on a swappable header, connected
//               to the P4 over SDIO and driven by ESP-Hosted. The module slot also accepts
//               ESP32-H2 / SX1262 LoRa / nRF24L01 / Wi-Fi HaLow variants.
//   - Storage:  microSD (TF) slot. Also: 11-pin GPIO header, Crowtail I2C/UART, camera header.
//   - Power:    5V / 2A via either USB-C port (one is a UART bridge, one is USB 2.0 device).
#pragma once

// --- Panel geometry (native landscape; no rotation needed) ---
#define LCD_WIDTH   1024
#define LCD_HEIGHT  600

// MIPI-DSI data/clock lanes are NOT in the GPIO matrix on the ESP32-P4 — they come out of
// dedicated MIPI pads and are configured by lane count, not pin number. So there are
// deliberately no DSI_* pin defines here; the DSI bus is set up in display.cpp by
// esp_lcd_new_dsi_bus() (2 data lanes). Only the panel's discrete control lines are GPIOs:
#define PIN_LCD_RST     41   // EK79007 reset
#define PIN_LCD_BLIGHT  31   // backlight, PWM (LEDC)

// --- Touch: GT911 on I2C ---
// Shared board I2C bus (also broken out to the Crowtail connectors).
#define PIN_I2C_SCL     46
#define PIN_I2C_SDA     45
#define PIN_TOUCH_RST   40
#define PIN_TOUCH_INT   42
#define GT911_ADDR_LOW  0x5D  // INT held low through reset (Elecrow's default)
#define GT911_ADDR_HIGH 0x14

// --- Audio: NS4168 amp -> 2 speakers, I2S TX. Used for UI feedback (clicks/ticks). ---
// AUDIO_CTRL gates amp power: drive it low when idle or the amp hisses and wastes current.
#define PIN_AUDIO_CTRL  30   // amp power enable (active high)
#define PIN_AUDIO_LRCLK 21   // WS
#define PIN_AUDIO_BCLK  22
#define PIN_AUDIO_SDATA 23
#define AUDIO_POWER_ENABLE   HIGH
#define AUDIO_POWER_DISABLE  LOW

// --- PDM microphone (present; not used by this unit yet) ---
#define PIN_MIC_CLK     24
#define PIN_MIC_SDIN    26

// --- Onboard LED ---
// The first unit in this project with a software-controllable LED (the nest has none).
#define PIN_LED         48

// --- Swappable radio module header (SPI) ---
// Only relevant if the C6 Wi-Fi module is swapped for LoRa/nRF. The C6 itself talks SDIO,
// not this SPI bus. Listed so nothing else claims these pins.
#define PIN_RADIO_SCK   8
#define PIN_RADIO_MISO  7
#define PIN_RADIO_MOSI  6
#define PIN_RADIO_BUSY  9    // SX1262
#define PIN_RADIO_NSS   10   // SX1262
