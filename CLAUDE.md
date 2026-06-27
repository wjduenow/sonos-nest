# sonos-nest — developer guide (read me first)

Firmware for a **standalone physical Sonos controller** on an **ELECROW CrowPanel 2.1" HMI
ESP32-S3 Rotary Display** (DHE03921D). It talks **directly** to Sonos speakers over the local
UPnP/SOAP API — no server, no cloud. PlatformIO + Arduino + LVGL 9.

- Full plan + feature scorecard + history: **`plans/01-sonos-knob-controller-plan.md`**
- Flashing from WSL (USB): **`docs/flashing-wsl.md`**
- Wireless flashing: the **`/ota` skill** (`.claude/skills/ota`)

## Build / flash

`pio` lives at `~/.platformio/penv/bin` — prepend it to PATH first:
```bash
export PATH="$PATH:$HOME/.platformio/penv/bin"
pio run -e crowpanel-rotary            # build the app
pio run -e crowpanel-rotary -t upload --upload-port /dev/ttyACMx   # USB flash
```
Build envs: **`crowpanel-rotary`** (the app), **`bringup`** (`-DPHASE0_BRINGUP` hardware
self-test), **`phase1`** (interactive SOAP test), **`ota`** (espota WiFi upload).

### WSL gotchas (this repo is developed on WSL2 — these will bite you)
- **USB needs usbipd.** From Windows admin PowerShell: `usbipd attach --wsl --busid <id>`.
  It **drops on every device reset/re-enumeration**, and the port number bumps
  (`/dev/ttyACM0` → `ACM1` …). Use `--auto-attach`, and resolve the port dynamically:
  `P=$(ls /dev/ttyACM* | head -1)`.
- **First USB flash needs download mode** (hold BOOT, tap RST, release BOOT). Later flashes
  auto-reset.
- **`pio device monitor` fails in non-interactive shells** ("Inappropriate ioctl for
  device"). Use the pyserial reader instead: `python3 tools/readser.py /dev/ttyACMx 30`
  (it reconnects across resets and asserts DTR). Boot prints race the reconnect — the
  reconnecting reader + a manual RST is the reliable way to catch them.
- **Transient GCC ICE** ("internal compiler error / Segmentation fault" in Arduino_GFX or
  FrameworkArduino): flaky, **just re-run `pio run`**. Not a real error.

### OTA (wireless flash)
Use the **`/ota` skill** — it checks the WSL firewall, finds the device IP (shown on the
on-device **Settings** screen), and runs espota with the password from `secrets.h`. Key
facts: device advertises as `sonos-nest` on UDP 3232; OTA needs **inbound-to-WSL allowed**
(mirrored-networking Hyper-V firewall) so the device can connect back; an OTA password is
required (`OTA_PASSWORD` in `secrets.h`). Laggy WiFi → retry; a failed transfer is harmless
(running firmware untouched, stall-reboots after 20s).

## Hardware — electrical (verified from Elecrow schematic + source — see `src/board_pins.h`)

> Physical/mechanical spec for designing mounts/cases (Ø79 rotating bezel, Ø58 rear
> body, 3×M3 Ø12-BC rear holes, USB-C on back): **`hardware/crowpanel-2.1-physical-spec.md`**.

- ESP32-S3R8: 240 MHz, **8 MB OPI PSRAM, 16 MB flash**.
- Display: **ST7701** 480×480 RGB-parallel. **Arduino_GFX pinned to 1.3.1** (older API:
  `Arduino_ESP32RGBPanel(CS,SCK,SDA,…)` + `Arduino_ST7701_RGBPanel`). **Do NOT bump** it
  without rewriting `display.cpp`.
- Touch: **CST816 @ 0x15** (not GT911). Encoder: EC11 on **GPIO42/4** (hardware PCNT).
- Knob press button: **PCF8574 expander @ 0x21, pin P5** (active-low). It's a stiff separate
  tact switch (K112) — needs a firm, centered push.
- **No software-controllable LED.** The only LED is a hard-wired power LED; GPIO43 is UART0
  TX, not an LED. Don't try to drive a status LED. (Real haptics would need a DRV2605 add-on.)
- Backlight: GPIO6 (LEDC). I2C: SDA 38 / SCL 39.

## Architecture
FreeRTOS tasks (shared `g_player` + `g_pending` guarded by `g_stateMutex`; use
`stateLock()`/`stateUnlock()`):
- **uiTask** (core 1, prio 3): LVGL render + input (`src/ui/screens.cpp uiTick`).
- **netTask** (core 0): drains `g_pending` commands + polls transport/position/volume ~1 Hz.
  `processPending()` is interleaved between poll SOAP calls to keep input snappy.
- **artTask** (core 0): album-art fetch + TJpg decode on track change.
- **loop()**: hosts `ArduinoOTA.handle()`. The UI task backs off to 120 ms while `otaActive()`.

Key modules:
- `src/sonos/` — `soap_client` (SOAP over keep-alive HTTP), `ssdp` (discovery), `didl` (parse).
- `src/library.{h,cpp}` — async ContentDirectory browse/play (Playlists `SQ:`, Favorites
  `FV:2`, Queue `Q:0`); `PlayMode` selects favorite=SetURI / playlist=enqueue / queue=Seek.
- `src/settings.{h,cpp}` — NVS (default room, brightness, cached zone IPs).
- `src/net/` — `wifi`, `ota`. `src/hw/` — display, touch, encoder, pcf8574.

UI screens (long-press knob = Menu hub): Now Playing (home), Rooms, Group, Playlists/
Favorites (shared browse list), Settings, Clock. **Swipe up** = queue, **swipe down** = clock.

## Gotchas that cost real time (don't rediscover these)
- **Group coordinator targeting:** transport/queue commands must go to the group
  **coordinator** IP, not the speaker. **Volume** is per-speaker. `ssdp.cpp` builds rooms from
  `GetZoneGroupState` (deduped, satellites excluded) and stores each room's coordinator IP.
- **DIDL is double-escaped:** unescape the SOAP layer, then unescape each field value again,
  or the album-art URL's `&` arrives as `&amp;` and the speaker returns nothing.
- **Album art is chunked HTTP:** read it with `HTTPClient::writeToStream()` (a raw stream
  read leaks chunk-size framing into the JPEG). Plain HTTP (no TLS). Buffer ≥220 KB
  (high-res covers). TJpg decodes **baseline only** (no progressive); buffers live in PSRAM.
- **`lv_conf.h` must be assembly-safe:** LVGL's `.S` files include it — no unguarded
  `#include <stdint.h>`. Set `LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE` (Xtensa).
- **LVGL memory pool (`LV_MEM_SIZE`):** scaled fonts (`transform_scale`, used for the big
  clock) allocate large ARGB draw layers from this pool; a long browse list can fill it and
  freeze the UI on a layer-alloc retry loop. It's at **96 KB**, and browse lists are freed on
  exit (`lv_obj_clean` + `library::clearResults()`). Watch this if adding big UI.
- **OTA:** the UI task must yield during `otaActive()` or the upload starves (~3% error);
  the overlay must **not** flush LVGL between progress steps (panel tears during flash writes).
- **Internal SRAM is the tight resource** (~150 KB free heap); flash (~4.9 MB/app slot,
  dual-OTA) and PSRAM (~7 MB free) are wide open.

## secrets.h (gitignored — `include/secrets.h`)
`WIFI_SSID`, `WIFI_PASS` required. Optional: `SONOS_DEFAULT_ROOM`, `CLOCK_TZ` (POSIX),
`OTA_PASSWORD`, `SONOS_ZONE_IP` (dev bypass). Template: `include/secrets.example.h`.

## Conventions
- Commit/push only when asked. Branch `main`, remote `origin` (github.com/wjduenow/sonos-nest).
- Keep `hardware/` (wall-mount) commits separate from firmware — the user owns that work.
- Test loop: build → flash (USB or `/ota`) → user confirms on device → commit + push.
