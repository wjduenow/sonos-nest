# Sonos Nest — ESP32 Rotary Knob Sonos Controller

> Development plan + research dossier. Written so a fresh session can pick up here with
> zero prior context. Target repo: `sonos-nest`. Architecture decision: **DIRECT control**
> (ESP32 speaks Sonos UPnP/SOAP itself — no dependency on any server).

---

## 1. Goal

Turn an **ELECROW CrowPanel 2.1" HMI ESP32 Rotary Display** into a standalone physical
Sonos controller.

### Core features (required)
- **Twist** the knob → adjust volume.
- **Push** the knob → play / pause.
- **Screen menus** to select modes:
  - Scroll + select **playlists** to play.
  - Scroll + select **radio stations** to play.
- While playing, **show current track info** (art, title, artist, progress) and allow
  **skip forward / backward**.

### Stretch features (explore)
- Multi-room / zone switcher with live activity indicators.
- Group / ungroup speakers.
- Clock screensaver on inactivity (round screen = great watch face), tap/twist to wake.
- Time-synced lyrics (LRCLIB API over HTTPS).
- OTA firmware updates (stable / nightly channels).
- WiFi captive-portal first-boot setup, creds persisted in NVS.
- Favorites quick-dial radial menu on long-press.
- Encoder acceleration for fast volume sweeps.
- Optional MQTT / Home Assistant exposure.

---

## 2. Hardware — ELECROW CrowPanel 2.1" HMI ESP32 Rotary Display

Model code on the box: **DHE03921D**. Product name: *CrowPanel 2.1inch-HMI ESP32 Rotary
Display 480×480 IPS Round Touch Knob Screen*.

| Spec | Detail |
|---|---|
| MCU | **ESP32-S3R8**, dual-core Xtensa 32-bit, 240 MHz |
| PSRAM / Flash | **8 MB PSRAM / 16 MB flash** |
| Display | 2.1" round IPS, **480×480**, **ST7701** driver, RGB parallel (24-bit) |
| Touch | Capacitive (GT911-class), reset/INT via I²C expander |
| Input | Rotary encoder + knob press (press is behind an I²C expander — see below) |
| Wireless | 2.4 GHz WiFi + BLE |
| Power / Program | USB-C, 5 V (power + flashing) |
| Expansion | UART, I²C, FPC connectors |
| Toolchains | Arduino IDE, PlatformIO, ESP-IDF, ESPHome, MicroPython; **LVGL** for UI |

### ⚠️ GPIO / pin map (verify against Elecrow schematic before coding)
```
Rotary encoder A : GPIO 42
Rotary encoder B : GPIO 4
Knob push button : PCF8574 I2C expander @ addr 0x21, pin P5  (NOT a direct GPIO!)
I2C SDA          : GPIO 38
I2C SCL          : GPIO 39
Backlight        : GPIO 6
LCD power        : PCF8574 P3
LCD reset        : PCF8574 P4
Touch reset      : PCF8574 P0
Touch interrupt  : PCF8574 P2
RGB control      : DE=GPIO40, VSYNC=GPIO7, HSYNC=GPIO15, PCLK=GPIO41
RGB data         : R0-R4, G0-G5, B0-B4 spread across GPIOs (get from Elecrow config)
```

**Critical implication:** the knob *press* is read over **I²C via the PCF8574 expander**,
not `digitalRead`. You must poll the expander (or use its INT line) — you cannot attach a
simple GPIO interrupt to the button. The encoder rotation *is* on direct GPIOs (42/4).

### Display perf note (board-specific)
The ST7701 is an **RGB-parallel** panel — it needs a framebuffer in PSRAM and the
`Arduino_GFX` / `esp32-s3 RGB LCD` peripheral. At 480×480×16bpp that's ~460 KB/frame; fits
in 8 MB PSRAM but bandwidth-limited at 240 MHz. Keep LVGL redraw regions small.

### Reference docs
- Wiki: https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html
- Product: https://www.elecrow.com/crowpanel-2-1inch-hmi-esp32-rotary-display-480-480-ips-round-touch-knob-screen.html
- HMI user manual PDF: https://www.elecrow.com/download/product/ESP32_Display/ESP32_Display_HMI_User_Manual.pdf
- CNX writeup: https://www.cnx-software.com/2025/09/19/elecrow-esp32-s3-rotary-displays-combine-round-ips-touchscreen-knob-and-press-input/
- Review: https://www.espboards.dev/blog/crowpanel-2-1-inch-rotary-display-review/

> **First action in fresh session:** clone Elecrow's example repo / SD-card demos for this
> exact board to get the *correct* ST7701 timing + full RGB pin map + touch + encoder init.
> Do not hand-derive the RGB data pins — use their config.

---

## 3. Architecture decision: DIRECT control

The ESP32 talks straight to the Sonos speakers over the **local UPnP/SOAP API** (port
**1400** per speaker). No server, no cloud, self-contained. (A proxy-via-existing-Flask-app
option was considered and rejected in favor of a standalone device.)

> Context: the user already has a separate Flask Sonos app (`sonos-sleep` repo) that wraps
> `soco` and exposes a REST API (`/room_status`, `/queue`, `/play`, `/volume`,
> `/sonos_playlist`, `/playlist_tracks`, etc.). That repo is a useful **behavioral
> reference** for what Sonos calls do, but the direct-control firmware does NOT depend on it.

### How Sonos local control works
- Discover speakers via **SSDP** (UDP M-SEARCH to `239.255.255.250:1900`, ST
  `urn:schemas-upnp-org:device:ZonePlayer:1`).
- Each speaker answers HTTP SOAP on `http://<zone-ip>:1400`.
- POST XML with a `SOAPACTION` header to a service control URL.

### Services / control URLs / actions needed
```
AVTransport    /MediaRenderer/AVTransport/Control
  Play, Pause, Next, Previous, Seek, SetAVTransportURI,
  GetPositionInfo (track + position + duration + DIDL metadata),
  GetTransportInfo (PLAYING/PAUSED/STOPPED)
  SOAPACTION ex: "urn:schemas-upnp-org:service:AVTransport:1#Play"

RenderingControl /MediaRenderer/RenderingControl/Control
  GetVolume, SetVolume, SetMute   (Channel=Master)
  SOAPACTION ex: "urn:schemas-upnp-org:service:RenderingControl:1#SetVolume"

ContentDirectory /MediaServer/ContentDirectory/Control
  Browse  → returns DIDL-Lite XML. Object IDs:
     SQ:               = Sonos playlists  (browse to enumerate)
     FV:2              = Sonos favorites
     R:0 / R:0/0       = radio favorites / TuneIn
  SOAPACTION: "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"
```

### Playing a playlist / favorite / radio (two-step, NOT just "Play")
1. `Browse` the container → parse DIDL-Lite → get each item's `res` URI + metadata.
2. `SetAVTransportURI` with the URI **and** its DIDL metadata, then `Play`.
   (For a playlist you typically clear queue, add the playlist's items / queue URI, then
   `Play` from queue index 0.)

### Now-playing freshness
- **Start with polling**: `GetTransportInfo` + `GetPositionInfo` + `GetVolume` every ~1–2 s.
- Later optional: **UPnP GENA event subscription** (push updates) — requires running a tiny
  HTTP callback server on the ESP32. More elegant, more code. Defer.

### The hard parts (where the time goes)
1. **ContentDirectory Browse + DIDL-Lite XML parsing** on-device (playlists/radio). Biggest
   chunk of work. Use lightweight string-scanning for the few tags you need, not a full DOM
   (RAM-constrained).
2. **Album art on ESP32-S3**: software JPEG decode only (no HW decoder). Use
   `TJpg_Decoder`, downscale to ~200–240 px, **decode once on track-change and cache** —
   never per poll. Skip dominant-color/bilinear fanciness initially.
3. **PCF8574 button polling** for the press.
4. SSDP discovery robustness across reboots.

---

## 4. Reference projects (de-risk — read these first)

| Project | What to take | License ⚠️ |
|---|---|---|
| **OpenSurface/SonosESP** — https://github.com/OpenSurface/SonosESP | Closest analog: full LVGL 9.5 touchscreen Sonos controller. Architecture, SOAP patterns, DIDL parsing, FreeRTOS task split, OTA, screensaver, lyrics, multi-zone. | **MIT** — copyable |
| **javos65/Sonos-ESP32** — https://github.com/javos65/Sonos-ESP32 | Drop-in Arduino lib: SSDP discovery (`CheckUPnP()`), play/pause/next/mute/volume, `getTrackTitle/Creator/Album`, `getFullTrackInfo`. Good for a Phase-1 spike. | **GPL** — do NOT copy into closed code; fine to spike/learn |
| **svrooij/sonos-api-docs** — https://github.com/svrooij/sonos-api-docs | Authoritative SOAP action + argument reference (AVTransport / RenderingControl / ContentDirectory). | docs |
| **svrooij sonos communication** — https://sonos.svrooij.io/sonos-communication | SOAP envelope format, headers, examples. | docs |

### SonosESP details worth knowing (it targets ESP32-**P4**, NOT our S3)
- P4: 400 MHz, 32 MB PSRAM, **hardware JPEG decoder**, ST7701 (4") / JD9165 (7"), GT911 touch.
  → Our S3 is weaker (240 MHz, 8 MB PSRAM, **software** JPEG). Don't copy its art pipeline
    wholesale; simplify for the S3.
- Threading: separate FreeRTOS tasks for **UI / album-art / lyrics / Sonos-poll**, mutex-guarded.
- Network: HTTPClient for SOAP, HTTPS for lyrics+art, UDP for SSDP. NVS for WiFi creds.
- Lyrics: **LRCLIB** API, time-synced LRC parsing, auto-hide.
- Screensaver: full-screen clock + (Unsplash) ambient bg on inactivity — **skip Unsplash on
  S3** (extra HTTPS + decode cost); plain clock is fine.
- OTA: version check, retry loop, progress UI, stable/nightly channels.
- UI scales across resolutions via `ui_scale.h` (relative sizing) — useful pattern.

---

## 5. Recommended tech stack
- **PlatformIO** + Arduino framework (espressif32 platform, board = esp32-s3-devkitc /
  custom 8MB-PSRAM-16MB-flash config; enable OPI PSRAM).
- **LVGL 8.4 or 9.x** for UI (lists, progress arc, image widget).
- **Arduino_GFX** (or ESP32 RGB LCD driver) for ST7701 panel.
- **HTTPClient** + **WiFiClientSecure** (SOAP / lyrics / art).
- **ArduinoJson** (config + any JSON APIs) and lightweight XML string-scan helpers for DIDL.
- **TJpg_Decoder** for album art JPEG.
- **Preferences (NVS)** for WiFi + settings persistence.
- FreeRTOS tasks with mutexes (built-in).

---

## 6. Firmware architecture sketch

```
Boot
 ├─ Init display (ST7701 RGB) + LVGL + touch + encoder + PCF8574
 ├─ WiFi: load NVS creds → connect; else captive-portal config screen (on-screen keyboard)
 └─ SSDP discover Sonos ZonePlayers → list zones (cache IPs)

FreeRTOS tasks (mutex-guarded shared "PlayerState"):
 • ui_task     – LVGL render + input (encoder GPIO42/4, button via PCF8574, touch)
 • poll_task   – every 1–2s: GetTransportInfo + GetPositionInfo + GetVolume
 • art_task    – on track change: fetch art URL → TJpg decode → cache LVGL image

SOAP client (HTTPClient → http://<zone-ip>:1400)
 • avtransport.{play,pause,next,prev,seek,setUri,getPosition,getTransport}
 • rendering.{getVolume,setVolume,setMute}
 • contentdir.browse(objectId) → parseDIDL() → [items]

Screens / state machine (LVGL):
 HOME (menu) ──twist=scroll, push=enter──┐
   ├─ NOW PLAYING  (default when active): art, title, artist, progress arc
   │     twist        = volume ±  → SetVolume
   │     short push   = play/pause
   │     touch L/R or long-twist = prev/next
   ├─ PLAYLISTS   – Browse SQ: → list; push=select → load queue → Play
   ├─ RADIO       – Browse R:0 / FV:2 → list; push=select → SetAVTransportURI → Play
   ├─ ROOMS       – zone switcher (which speaker the knob controls)
   └─ SETTINGS    – room default, brightness, WiFi, OTA, about
 Inactivity → CLOCK screensaver; any input wakes.
```

---

## 7. Phased build plan

### Phase 0 — Bring-up (½ day)
- Flash Elecrow's LVGL demo for THIS board. Confirm: display renders, touch works,
  encoder counts (GPIO 42/4), button reads via PCF8574 (0x21, P5), backlight (GPIO 6).
- Get the exact ST7701 timings + RGB pin map from Elecrow's config. **Gate everything else
  on this working.**

### Phase 1 — Talk to Sonos (1 day) — *recommended starting point*
- SSDP discovery → print discovered zone IPs.
- Minimal SOAP: send `Play` / `Pause`, read `GetTransportInfo`, against a hardcoded zone IP.
- Spike with **javos65 lib** first to validate your network/speakers fast, then decide
  build-vs-borrow for the real client (mind GPL if copying).
- **Exit criteria:** the board can pause/resume real music on your Sonos.

### Phase 2 — Now Playing (2–3 days)
- Poll transport/position/volume; render title/artist/progress arc.
- Inputs: twist→SetVolume, push→play/pause, touch/long-twist→next/prev.
- Album art: fetch + `TJpg_Decoder` + downscale + **cache on track-change**.
- Move network calls off the UI task (FreeRTOS) so the knob stays responsive.

### Phase 3 — Browse & select (3–4 days) — *the big one*
- ContentDirectory `Browse` for `SQ:` (playlists) and `R:0`/`FV:2` (radio/favorites).
- Parse DIDL-Lite → scrollable LVGL lists.
- Select → (playlists: clear queue, add, Play index 0) / (radio: SetAVTransportURI + Play).

### Phase 4 — Polish (ongoing)
- ROOMS zone switcher, group/ungroup.
- Clock screensaver on inactivity.
- OTA updates (stable/nightly).
- LRCLIB time-synced lyrics.
- Settings persistence (NVS), reconnect/error states, screen dim/sleep.
- Optional: encoder acceleration, favorites radial, MQTT/HA.

### Effort note
Direct control is ~2–3× the proxy-approach firmware effort, almost all of it in Phase 3
(Browse + DIDL parsing). Payoff: a self-contained knob with no server dependency.

---

## 8. Open questions / decisions for next session
- [ ] Pull Elecrow's exact board config (ST7701 timing + full RGB pin map) — **blocker for Phase 0.**
- [ ] Confirm PCF8574 address (0x21) and button pin (P5) against the real schematic.
- [ ] Default Sonos room/zone, and whether multi-zone is needed at v1.
- [ ] LVGL 8.x vs 9.x (SonosESP uses 9.5; ensure ST7701 driver compatibility).
- [ ] Build-vs-borrow the SOAP client (GPL javos65 lib vs MIT SonosESP patterns vs own).
- [ ] Polling vs GENA event subscription for now-playing (start polling).

## 9. Repo setup suggestion (sonos-nest)
```
sonos-nest/
  platformio.ini
  src/
    main.cpp
    sonos/        # soap client, ssdp discovery, didl parser
    ui/           # lvgl screens, ui_scale.h
    hw/           # display/touch/encoder/pcf8574 init
    net/          # wifi, ota
  plans/
    01-sonos-knob-controller-plan.md   ← this file
  lib/
  README.md
```

---

*Research compiled 2026-06-24. Device confirmed as CrowPanel 2.1" ESP32-S3 rotary display
(DHE03921D). Architecture: direct UPnP/SOAP control. Primary reference: OpenSurface/SonosESP (MIT).*
