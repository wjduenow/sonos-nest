// Board pin map — Hosyond / LCDWIKI ES3C28P 2.8" ESP32-S3 display board.
//
// STUB. The mechanical/board spec is verified (hardware/rec-2.8/README.md +
// ES3C28P_Size.pdf), but the GPIO assignments below are NOT yet pinned from a schematic —
// they are placeholders for the real bring-up. Do not trust these numbers; caliper/scope/
// datasheet-verify before wiring the ILI9341 / FT6336G driver in board.cpp.
//
// Known-good facts (from the board spec):
//   - MCU:     ESP32-S3R8 (8 MB OPI PSRAM, 16 MB flash) — same class as the nest board
//   - Display: ILI9341V, 4-wire SPI, 240x320
//   - Touch:   FT6336G, I2C @ 0x38
//   - Extras:  microSD (SDIO), front MEMS mic, addressable RGB LED on IO42
#pragma once

// --- Panel geometry (correct; orientation TBD) ---
#define LCD_WIDTH   240
#define LCD_HEIGHT  320

// --- Touch (address is correct; INT/RST pins TBD) ---
#define TOUCH_ADDR  0x38   // FT6336G

// --- TODO: real GPIO assignments (I2C SDA/SCL, SPI SCK/MOSI/MISO/CS/DC/RST, backlight,
//     SD CS/CLK/MOSI/MISO, RGB-LED IO42, mic). Fill from the ES3C28P schematic during
//     board bring-up; board.cpp does not use them yet. ---
