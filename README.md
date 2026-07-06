# sonos-nest

Firmware for **standalone physical Sonos controllers** — small ESP32-S3 appliances that talk
**directly** to Sonos speakers over the local UPnP/SOAP API (no server, no cloud). One
codebase drives multiple hardware units and UX treatments from a shared core.

Full design + research dossier: [`plans/01-sonos-knob-controller-plan.md`](plans/01-sonos-knob-controller-plan.md).
Multi-unit reorg rationale: [`plans/02-multi-unit-reorg.html`](plans/02-multi-unit-reorg.html).

## Units

| Unit | Board | Form factor / UX | Use case | Build env |
|------|-------|------------------|----------|-----------|
| **sonos-nest** | ELECROW CrowPanel 2.1" (ST7701 480×480 round, EC11 rotary encoder + knob button, CST816 touch, PCF8574 expander) | Round rotary knob — twist = volume, press = play/pause, touch/gesture to browse | Wall-mounted knob controller | `nest` |
| **sonos-sleep-machine** | Hosyond/LCDWIKI ES3C28P 2.8" (ILI9341 240×320 SPI, FT6336 touch, microSD, mic, RGB-LED) | Rectangular, touch-first | Nightstand / countertop (clock, alarms, sleep timer, ambient — **UX TBD**) | `sleep-machine` |

Both run the same **ESP32-S3R8** (8 MB OPI PSRAM, 16 MB flash) and share all Sonos control,
discovery, browsing, settings, networking, and OTA. They differ only in their board drivers
and screen UX. `sonos-sleep-machine` is currently a **stub** (board + UX skeleton that
compiles and shows now-playing); its real driver and UX are deferred.

## Architecture — shared core + pluggable board/unit

```
src/
  main.cpp              thin entry: boardInit → uiInit → appBoot → appStartTasks
  core/                 device-agnostic — compiled into EVERY unit
    app.{h,cpp}         controller: zone select, command dispatch, poll tasks (ui/net/art)
    player_state.{h,cpp}  mutex-guarded shared now-playing state + pending commands
    library.{h,cpp}     async ContentDirectory browse/play (playlists/favorites/queue)
    settings.{h,cpp}    NVS (default room, brightness, cached zone IPs)
    album_art.{h,cpp}   art fetch + TJpg decode → LVGL image (off the UI thread)
    board.h             HAL contract every board implements
    unit.h              UX contract every unit implements (uiInit/uiTick)
    sonos/              soap_client · ssdp (discovery) · didl (DIDL-Lite parser)
    net/                wifi · ota
  boards/               one dir per board — implements board.h
    crowpanel_rotary/   ST7701 display · CST816 touch · EC11 encoder · PCF8574 · pins.h
    es3c28p/            STUB: pins.h + board.cpp (ILI9341/FT6336 = TODO)
  units/                one dir per UX — implements unit.h
    sonos_nest/         round/rotary LVGL screens + ui_scale.h (480×480)
    sleep_machine/      STUB: placeholder screen + ui_scale.h (240×320)
hardware/               3D-printed mounts/cases (user-owned; see hardware/README.md)
plans/                  design plans + reorg doc
docs/                   flashing-wsl.md
include/                lv_conf.h, secrets.h (gitignored)
```

**How the seam works:** the UI never calls Sonos/SOAP directly — it reads/writes the
mutex-guarded globals `g_player` / `g_pending` (`core/player_state.h`) and the
`library::` / `sonos::` / `settings*` APIs. A unit talks to hardware only through the free
functions in `core/board.h` (`boardInit`, `backlightSet`, `encoderDelta`, `knobEvent`…).
Each PlatformIO env selects exactly one board + one unit + the core via `build_src_filter`,
and sets `-DDEVICE_HOSTNAME` (per-unit mDNS/OTA name). Adding a new form factor = a new
`boards/<board>/` + `units/<unit>/` + one env; the core is untouched.

## Build / flash

[PlatformIO](https://platformio.org/) + Arduino. Libraries: LVGL 9.x, GFX Library for
Arduino (pinned v1.3.1 — has both ST7701 RGB and ILI9341 SPI drivers), ArduinoJson,
TJpg_Decoder, ESP32Encoder.

```bash
pio run -e nest                       # build the nest app (default env)
pio run -e nest -t upload --upload-port /dev/ttyACMx   # USB flash
pio run -e sleep-machine              # build the (stub) sleep-machine app
```

Env variants: `nest-bringup` (Phase-0 hardware self-test), `nest-phase1` (interactive SOAP
test), `nest-ota` / `sleep-machine-ota` (WiFi flash via espota).

On **WSL2 (Windows)**, USB needs bridging first — see
[`docs/flashing-wsl.md`](docs/flashing-wsl.md). Wireless flashing uses the `/ota` skill.

### First-time setup

1. `pio run -e nest` once to fetch libraries.
2. Copy `include/secrets.example.h` → `include/secrets.h` and fill in WiFi (and optional
   `SONOS_DEFAULT_ROOM`, `OTA_PASSWORD`, `CLOCK_TZ`).

## Hardware (enclosures)

3D-printed mounts and cases live in [`hardware/`](hardware/) — one directory per unit
(`round-nest-2.8/` for the round CrowPanel, `rec-2.8/` for the ES3C28P), generated with a
Python CSG toolchain. This is user-owned mechanical work kept **separate** from firmware
commits. See [`hardware/README.md`](hardware/README.md).
