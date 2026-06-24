# sonos-nest

Standalone physical Sonos controller on an **ELECROW CrowPanel 2.1" HMI ESP32-S3 Rotary
Display** (DHE03921D). Twist the knob for volume, push for play/pause, and browse playlists
/ radio on a 480×480 round touchscreen. The firmware talks **directly** to Sonos speakers
over the local UPnP/SOAP API — no server, no cloud.

Full design + research dossier: [`plans/01-sonos-knob-controller-plan.md`](plans/01-sonos-knob-controller-plan.md).

## Hardware

- **MCU:** ESP32-S3R8 (240 MHz, 8 MB OPI PSRAM, 16 MB flash)
- **Display:** 2.1" round IPS 480×480, ST7701 RGB-parallel driver
- **Input:** rotary encoder (GPIO 42/4) + knob press (PCF8574 expander @ 0x21, P5) + GT911 touch

## Toolchain

[PlatformIO](https://platformio.org/) + Arduino framework. Libraries: LVGL 9.x,
GFX Library for Arduino (ST7701), ArduinoJson, TJpg_Decoder.

```bash
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial @ 115200
```

### First-time setup

1. `pio run` once to fetch libraries.
2. Create `include/lv_conf.h` — see [`include/README.md`](include/README.md).
3. Copy `include/secrets.example.h` → `include/secrets.h` and fill in WiFi (dev only).

## Project layout

```
platformio.ini          build config (ESP32-S3, OPI PSRAM, 16MB flash)
include/                 lv_conf.h, secrets.h (gitignored)
src/
  main.cpp               boot + FreeRTOS task launch (ui / poll / art)
  board_pins.h           GPIO + PCF8574 pin map  ⚠️ verify vs Elecrow schematic
  player_state.{h,cpp}   mutex-guarded shared now-playing state
  hw/                    display (ST7701) · touch · encoder · pcf8574
  sonos/                 soap_client · ssdp (discovery) · didl (DIDL-Lite parser)
  ui/                    lvgl screens · ui_scale.h
  net/                   wifi · ota
plans/                   design plan + research
```

## Status

Scaffold only — modules are stubbed with `TODO` markers keyed to the phased build plan
(§7 of the plan). Build order: **Phase 0** bring-up (gate: flicker-free redraw with WiFi
active) → **Phase 1** talk to Sonos → **Phase 2** now playing → **Phase 3** browse/select
→ **Phase 4** polish.
