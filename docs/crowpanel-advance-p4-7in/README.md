# CrowPanel Advance 7" ESP32-P4 — vendor documentation

Reference material for the **sonos-jukebox** board (Elecrow SKU **DHE04107D**).
Plan + architecture: `plans/07-sonos-jukebox.md`. Pin map: `src/boards/crowpanel_p4_7in/pins.h`.

## The PDFs are gitignored — fetch them

They total ~78 MB, so they are not committed. Run `./fetch-docs.sh` in this directory to
download them, or grab them individually from the links below.

| File | Size | What it's good for |
|---|---|---|
| `User_Manual(HMI_Advance_ESP32-P4).pdf` | 28 MB | Board overview, connectors, spec table |
| `Arduino_Lessons_for_CrowPanel_Advanced_7inch_ESP32-P4_HMI.pdf` | 18 MB | **The pin map.** Lessons walk through `board_config.h` pin by pin (GPIO, touch, backlight, audio, radio). This is where `pins.h` came from. Also names the exact library versions Elecrow targets. |
| `Advance_HMI_P4_7inch_Course.pdf` | 33 MB | Same material for ESP-IDF instead of Arduino |
| `esp32-p4_datasheet_en.pdf` | 1.5 MB | Espressif ESP32-P4 datasheet (v0.5 pre-release) |

## Source links

- Product page —
  <https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html>
- Wiki —
  <https://www.elecrow.com/wiki/CrowPanel_Advanced_7inch_ESP32-P4_HMI_AI_Display_1024x600_IPS_Touch_Screen_with_WiFi6_Compatible_with_ArduinoLVGL.html>
- **GitHub (schematics, PCB, 3D model, datasheets, factory firmware)** —
  <https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen>
  Contains the Eagle `.sch`/`.brd` and a `.stp` model — **use these, not the PDFs, when
  designing the case or confirming a pin.**
- C6 firmware upgrade guide (ESP-Hosted co-processor) —
  <https://www.elecrow.com/download/product/DHE04107D/CrowPanel_Advance_ESP32-P4%20Display_Firmware_Upgrade_Guide.zip>

## Facts worth knowing without opening a PDF

- **ESP32-P4NRW32**: RISC-V dual-core HP @ 360 MHz (400 MHz on rev.300), LP core @ 40 MHz,
  16 MB flash, 32 MB in-package PSRAM, **768 KB L2MEM**.
- **The P4 has no radio.** Wi-Fi/BLE come from an **ESP32-C6-MINI-1** on a swappable header,
  connected over **SDIO** and driven by **ESP-Hosted**. The slot also takes ESP32-H2 / SX1262
  LoRa / nRF24L01 / Wi-Fi HaLow modules.
- Display is **MIPI-DSI** with an **EK79007** driver IC — the DSI lanes are dedicated MIPI pads,
  *not* GPIO-matrix pins, so they don't appear in `pins.h`.
- Touch is **GT911**; its I2C address is latched from the INT pin level as RESET releases
  (INT low → `0x5D`, INT high → `0x14`).
- Elecrow's own examples target **Arduino-ESP32 3.3.3**, `esp32_display_panel` **1.0.4** and
  **LVGL 8.3.11**. This repo is on **LVGL 9**, so their `lvgl_v8_port.cpp` is not reusable —
  see the plan.
- PCB is **180 × 105 mm**; active display area **155 × 87 mm**.
