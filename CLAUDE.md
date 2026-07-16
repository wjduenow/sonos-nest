# sonos-nest — developer guide (read me first)

Firmware for **standalone physical Sonos controllers** — ESP32-S3 appliances that talk
**directly** to Sonos speakers over the local UPnP/SOAP API (no server, no cloud).
PlatformIO + Arduino + LVGL 9. One **shared core** drives multiple hardware **units**:

- **sonos-nest** (`nest` env) — the original: ELECROW CrowPanel 2.1" round rotary display
  (ST7701 480×480, EC11 encoder + knob, CST816 touch, PCF8574 expander).
- **sonos-sleep-machine** (`sleep-machine` env) — a nightstand sleep-sound player: rectangular
  2.8" Hosyond/LCDWIKI ES3C28P (ILI9341 240×320 SPI, FT6336 touch, microSD, ES8311 codec +
  speaker, mic, RGB-LED). **Working**: display, touch, SD, on-device MP3 playback, HTTP media
  server + remote SD management, and a touch UX (home carousel, rooms, WiFi, track picker,
  settings, sleep timer). The **mic captures** (ES8311 ADC record path proven via the
  `sleep-machine-mic` bring-up env — see below), but is not yet wired into the app UX.
  **Not yet wired**: the RGB-LED.

Both share all Sonos control/discovery/browse/settings/net/OTA; they differ only in
`src/boards/<board>/` (drivers) and `src/units/<unit>/` (UX). See **Architecture** below.

- Full plan + feature scorecard + history: **`plans/01-sonos-knob-controller-plan.md`**
- Multi-unit reorg rationale + layout: **`plans/02-multi-unit-reorg.html`**
- Flashing from WSL (USB): **`docs/flashing-wsl.md`**
- Wireless flashing: the **`/ota` skill** (`.claude/skills/ota`)

## Build / flash

`pio` lives at `~/.platformio/penv/bin` — prepend it to PATH first:
```bash
export PATH="$PATH:$HOME/.platformio/penv/bin"
pio run -e nest            # build the nest app (default env)
pio run -e nest -t upload --upload-port /dev/ttyACMx   # USB flash
pio run -e sleep-machine   # build the sleep-machine app
```
Envs: **`nest`** / **`sleep-machine`** (the apps), **`nest-bringup`** (`-DPHASE0_BRINGUP`
hardware self-test), **`nest-phase1`** (interactive SOAP test), **`nest-ota`** /
**`sleep-machine-ota`** (espota WiFi upload). Also standalone es3c28p bring-ups: **`sleep-machine-audio`**
(ES8311 speaker playback), **`sleep-machine-mic`** (ES8311 mic capture — prints a live level meter),
**`sleep-machine-sdreader`** (SD-as-USB), **`sleep-machine-wake`** (microWakeWord wake-word trial —
mic → TFLM; see Phase-1 note below). Each env selects one board + one unit + the
core via `build_src_filter` and sets `-DDEVICE_HOSTNAME` (per-unit mDNS/OTA name).

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
facts: each unit advertises its `DEVICE_HOSTNAME` (`sonos-nest` / `sonos-sleep`) on UDP
3232 — distinct names so two units don't collide; OTA needs **inbound-to-WSL allowed**
(mirrored-networking Hyper-V firewall) so the device can connect back; an OTA password is
required (`OTA_PASSWORD` in `secrets.h`). Laggy WiFi → retry; a failed transfer is harmless
(running firmware untouched, stall-reboots after 20s).

## Hardware — electrical, nest board (verified from Elecrow schematic + source — see `src/boards/crowpanel_rotary/pins.h`)

> Physical/mechanical spec for designing mounts/cases (Ø79 rotating bezel, Ø58 rear
> body, 3×M3 Ø12-BC rear holes, **4-pin MX1.25 JST on the back — not USB-C**):
> **`hardware/round-nest-2.8/crowpanel-2.1-physical-spec.md`**.

- ESP32-S3R8: 240 MHz, **8 MB OPI PSRAM, 16 MB flash**.
- Display: **ST7701** 480×480 RGB-parallel. **Arduino_GFX pinned to 1.3.1** (older API:
  `Arduino_ESP32RGBPanel(CS,SCK,SDA,…)` + `Arduino_ST7701_RGBPanel`). **Do NOT bump** it
  without rewriting `boards/crowpanel_rotary/display.cpp`. (1.3.1 also ships
  `Arduino_ESP32SPI` + `Arduino_ILI9341`, which the es3c28p board uses — no bump needed.)
- Touch: **CST816 @ 0x15** (not GT911). Encoder: EC11 on **GPIO42/4** (hardware PCNT).
- Knob press button: **PCF8574 expander @ 0x21, pin P5** (active-low). It's a stiff separate
  tact switch (K112) — needs a firm, centered push.
- **No software-controllable LED.** The only LED is a hard-wired power LED; GPIO43 is UART0
  TX, not an LED. Don't try to drive a status LED. (Real haptics would need a DRV2605 add-on.)
- Backlight: GPIO6 (LEDC). I2C: SDA 38 / SCL 39.

## Architecture

**Three layers, selected per-env by `build_src_filter`:** `src/core/` (device-agnostic,
in every build) + one `src/boards/<board>/` (drivers, implements `core/board.h`) + one
`src/units/<unit>/` (UX, implements `core/unit.h` = `uiInit`/`uiTick`). `src/main.cpp` is a
thin wire-up: `boardInit()` → `uiInit()` → `appBoot()` → `appStartTasks()`, and `loop()`
pumps `otaHandle()`. **The UI never calls Sonos/SOAP or board pins directly** — it uses the
mutex-guarded globals `g_player`/`g_pending` and the `library::`/`sonos::`/`settings*` APIs,
and reaches hardware only through `core/board.h` free functions (`backlightSet`,
`encoderDelta`, `knobEvent`…; touch is a push-based LVGL indev registered in `boardInit()`).

FreeRTOS tasks live in **`src/core/app.cpp`** (shared `g_player` + `g_pending` guarded by
`g_stateMutex`; use `stateLock()`/`stateUnlock()`), created by `appStartTasks()`:
- **uiTask** (core 1, prio 3): LVGL render + input — calls the unit's `uiTick()`
  (`src/units/<unit>/screens.cpp`).
- **netTask** (core 0): drains `g_pending` commands + polls transport/position/volume ~1 Hz.
  `processPending()` is interleaved between poll SOAP calls to keep input snappy.
- **artTask** (core 0): album-art fetch + TJpg decode on track change.
- **loop()**: hosts `ArduinoOTA.handle()`. The UI task backs off to 120 ms while `otaActive()`.

Key modules (all under `src/core/` — device-agnostic):
- `core/app.{h,cpp}` — the controller: zone/coordinator selection, `processPending`, the
  three tasks; exposes `appBoot()` (wifi/time/OTA/discovery/zone-pick) + `appStartTasks()`.
- `core/sonos/` — `soap_client` (SOAP over keep-alive HTTP), `ssdp` (discovery), `didl` (parse).
- `core/library.{h,cpp}` — async ContentDirectory browse/play (Playlists `SQ:`, Favorites
  `FV:2`, Queue `Q:0`); `PlayMode` selects favorite=SetURI / playlist=enqueue / queue=Seek.
- `core/settings.{h,cpp}` — NVS (default room, brightness, cached zone IPs).
- `core/webconfig.{h,cpp}` — what a board's web UI may configure (sleep/wake track, Sonos room)
  and how to apply it (NVS + `g_pending`). **Boards must not reach into settings/`g_pending`
  themselves** — a board's HTTP server does sockets + routing and calls this. Also clears a
  sleep/wake pick when its file is deleted, so a pick can't dangle.
- `core/album_art.{h,cpp}` — art fetch + TJpg decode → LVGL image (form-factor-agnostic).
- `core/net/` — `wifi`, `ota` (OTA hostname = `DEVICE_HOSTNAME` macro, set per env).
- `core/board.h` / `core/unit.h` — the HAL + UX contracts.

Boards: `src/boards/crowpanel_rotary/` (display · touch · encoder · pcf8574 · pins.h ·
bringup · phase1_test) and `src/boards/es3c28p/` (display · touch · sd_card · local_audio
(ES8311 + Helix MP3) · local_stream (httpd) · local_tracks · pins.h; `sd_msc`, `audio_test`, and
`mic_test` are standalone bring-up envs, excluded from the app build). Units: `src/units/sonos_nest/`
(round/rotary screens + ui_scale.h) and `src/units/sleep_machine/` (touch screens + ui_scale.h).

nest UI screens: Now Playing (home), Menu hub, Rooms, Group, Playlists/Favorites (shared browse
list), Settings, Clock. From Now Playing: **swipe right** (drag in from the left edge) = Menu,
**swipe up** = queue, **swipe down** = clock; twist = volume, press = play/pause. On list
screens, long-press = back. (The Menu is *not* a knob long-press — some in-tree comments still
claim it is; they're stale.)

sleep-machine UI: a 3-way home carousel (swipe left/right) picking where a track plays —
**Sonos** (speaker's own library), **stream to Sonos** (SD file served over HTTP, see below),
or **on-device speaker** (SD → Helix MP3 → ES8311) — plus a sleep timer and a screensaver that
dims while playing. **Now Playing** has Stop, a volume row (`[-]` slider `[+]`, 2%/tap, driving
Sonos *or* the codec depending on the active output), and a **Wake** button that swaps the
playing track for the wake track *on the output already in use*, at the current volume, looping.
Settings: brightness, Sonos room, Sleep Track, Wake Track (same picker, retitled), Wi-Fi, device
name, and **File Manager** (read-only — shows `localManagerUrl()` so you can type it into a
browser). The Wake button appears only when there's something to switch *to*: it hides when the
card has no wake track, and once the wake track is what's playing (including when the Sleep and
Wake tracks are the same file). With no explicit pick, any file named `wake` (case-insensitive)
is used, so a dropped-in `Wake.mp3` just works.

### sleep-machine HTTP server (`boards/es3c28p/local_stream.cpp`) — TWO ports, on purpose
Started from `boardInit()` (its task waits for WiFi itself, since `appBoot()` connects later).

- **:8080 — `WebServer`.** The SD-management UI at `/` (embedded HTML: list/upload/delete, plus
  dropdowns for the sleep track, wake track and Sonos room), `GET /api/tracks`,
  `GET|POST /api/config` (thin wrappers over `core/webconfig`), `POST /api/delete?name=`, and
  `GET /ocean.mp3` — the fixed route Sonos streams from (`localFileUrl()` in the board HAL points
  it at the real on-card filename, so the URL handed to Sonos has no spaces to escape).
- **:8081 — a bare `WiFiServer`.** Uploads only (`POST /upload?name=<basename>`, raw body).

The board HAL exposes the base URL as `localManagerUrl()` (nullptr on boards without local
storage, or before WiFi is up); Settings → **File Manager** just displays it. The port lives in
the board, not the UI.

**Uploads deliberately bypass `WebServer` — do not "simplify" this back.** Both of its body
paths are broken for our files on framework-arduinoespressif32 @ 3.20017: multipart reads the
body **one byte at a time** (`Parsing.cpp _uploadReadByte`), and the raw path never calls
`_parseArguments()` (so `arg("name")` is always empty → every upload refused) *and* its
`readBytes(buf, HTTP_RAW_BUFLEN)` blocks for a **full** buffer, hanging on any file whose size
isn't an exact multiple of it. So we own the read loop instead.

Consequences worth knowing:
- Two ports = **cross-origin**, so the upload socket answers the `OPTIONS` preflight *and* the
  page sends `Content-Type: text/plain` (CORS-safelisted) to avoid triggering one. Test uploads
  with a browser or a curl that sets `Origin` — plain curl skips preflight and hides the bug.
- Upload speed is **the SD card** (~180 KB/s write), not the network or block size (16 KB and
  64 KB measured identical). A few-minute track is ~5 MB / ~50 s; hour-long ones take ~14 min.
- Upload/delete are refused (409) while `localAudioActive()` — the audio task streams MP3 off
  the same card, and writing underneath it glitches playback.
- Uploads are restricted to a bare `.mp3` basename (no dirs, no `..`); `localTracks*` only
  scans the card root, and `localTracksRefresh()` is called after every mutation.

## Gotchas that cost real time (don't rediscover these)
- **Group coordinator targeting:** transport/queue commands must go to the group
  **coordinator** IP, not the speaker. **Volume** is per-speaker. `ssdp.cpp` builds rooms from
  `GetZoneGroupState` (deduped, satellites excluded) and stores each room's coordinator IP.
- **DIDL is double-escaped:** unescape the SOAP layer, then unescape each field value again,
  or the album-art URL's `&` arrives as `&amp;` and the speaker returns nothing.
- **Sonos caches by URL — `localFileUrl()`'s `?v=N` is load-bearing, don't "clean it up".** The
  media route is a *fixed* path (`/ocean.mp3`), so every file would otherwise get the same URL,
  and Sonos keys both content **and** metadata off it. Without the counter, swapping the sleep
  track for the wake track re-plays the **cached sleep track** — the swap looks wired up and
  silently does nothing. `WebServer` strips the query before matching, so the route still hits.
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
- **Wake word (microWakeWord) — WORKS on real voice with custom-trained models.**
  `sleep-machine-wake` (`wake_test.cpp`) runs mic → TFLM microfrontend (40-ch log-mel, 30 ms/10 ms,
  int8 `real×9.8−128` quant) → streaming model (3-frame windows, resource-variable state). TFLM is
  **vendored** in `lib/tflm` (esp-tflite-micro + classic microfrontend, reference kernels — Arduino
  has no working TFLM package for streaming models; see `lib/tflm/VENDORING.md`). The three custom
  "Kinder Bedtime / Wake-Up / Rise and Shine" models (`models/kinder_*`, trained per
  `training/wake-word/`) each fire at **0.95-1.00 on real voice**, silent on the other phrases.
  Stock TTS-trained models (okay_nabu) peaked at only 5-20/255 on a real voice — custom training is
  what closed that gap. Key gotchas that cost time: TFLM needs `-fno-exceptions` (private
  `operator delete` + placement-new); the inference must run in a **separate task** off a feature
  queue or it starves I2S capture (stale audio); capture stereo and take the LEFT slot (REG44=0x50
  routes the mono ADC there); arena goes in PSRAM (~19 KB/model).
- **Wake word: only ONE model fits real-time until esp-nn is vendored.** Each inference is ~17 ms
  (TFLM *reference* kernels) and a 3-frame feature window arrives every 30 ms, so running all three
  costs 51 ms/30 ms = 170% of real-time → the feature queue overflows (`drops` climbs) and the
  models see discontinuous audio. `kOnlyModel` in `wake_test.cpp` selects one. Restoring the
  `esp_nn` kernels (deleted during vendoring) should give ~2-4x and make 3-up viable.
- **ES8311 mic (es3c28p) needs an explicit ADC power-up — `arduino-audio-driver` only inits for
  playback.** Setting I2S RX + ADC volume/gain yields a DC-centered dither floor that ignores
  sound; you must also power the analog ADC (`REG0E=0x02`, its suspend value `0xFF`=off), enable
  the ADC clock (`REG01=0x3F`), route ADC data (`REG44=0x50`), unmute ADC onto SDOUT, select the
  analog mic (`REG14=0x1A`), and give it real gain (low-output mic — analog PGA `REG16` near max
  is best SNR). Full sequence + a live level meter: `mic_test.cpp` / the `sleep-machine-mic` env.

## secrets.h (gitignored — `include/secrets.h`)
`WIFI_SSID`, `WIFI_PASS` required. Optional: `SONOS_DEFAULT_ROOM`, `CLOCK_TZ` (POSIX),
`OTA_PASSWORD`, `SONOS_ZONE_IP` (dev bypass). Template: `include/secrets.example.h`.

## Conventions
- Commit/push only when asked. Branch `main`, remote `origin` (github.com/wjduenow/sonos-nest).
- Keep `hardware/` commits separate from firmware — the user owns that work. Layout:
  `hardware/round-nest-2.8/` (original round CrowPanel unit: `wall/` mount) and
  `hardware/rec-2.8/` (ES3C28P rectangular board: `countertop/` nightstand stand).
- Test loop: build → flash (USB or `/ota`) → user confirms on device → commit + push.
