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

### ⚠️ Display perf note (board-specific) — this is a Phase-0 GATE, not a tuning detail
The ST7701 is an **RGB-parallel** panel — it needs a framebuffer in PSRAM and the
`Arduino_GFX` / `esp32-s3 RGB LCD` peripheral. At 480×480×16bpp that's ~460 KB/frame; fits
in 8 MB PSRAM but bandwidth-limited at 240 MHz.

Driving an RGB panel from a PSRAM framebuffer on the S3 is *the* classic source of
tearing/flicker, and that PSRAM/peripheral bandwidth competes directly with WiFi/SOAP
traffic on the same SoC. If redraw can't stay clean while the network is busy, it changes
everything downstream (bounce buffers, double- vs direct-buffer, smaller redraw regions).
→ **Promote to a Phase-0 exit gate:** *sustained flicker-free redraw while WiFi is active.*
Keep LVGL redraw regions small; expect to tune bounce-buffer size and PSRAM clock.

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

### Playing a playlist / favorite / radio — TWO DISTINCT FLOWS (don't conflate)
These are not the same sequence; Phase 3 must implement both separately.

**A. Radio / favorite (single stream):**
1. `Browse` the container → parse DIDL-Lite → get the item's `res` URI + metadata.
2. `SetAVTransportURI` with the URI **and** its DIDL metadata, then `Play`. Done.

**B. Sonos playlist (`SQ:n`, multi-track queue) — more than clear/add/Play:**
1. `Browse` `SQ:` to enumerate playlists; get the chosen container's URI/metadata.
2. (Optional) `RemoveAllTracksFromQueue` to clear.
3. `AddURIToQueue` with the container URI + metadata (enqueues all its tracks).
4. `SetAVTransportURI` to the **queue itself**: `x-rincon-queue:RINCON_<uuid>#0`.
5. `Seek` (Unit=TRACK_NR) to track 0, then `Play`.

### ⚠️ Group coordinator targeting (will bite you the moment speakers are grouped)
In a Sonos group, **all transport + queue actions must target the group coordinator**, not
an arbitrary member. Sending `Play`/`Next`/`AddURIToQueue` to a non-coordinator fails or
misbehaves. Volume can be per-speaker or group-wide. Before any transport call:
- Query topology (`ZoneGroupTopology` service, or `http://<ip>:1400/status/topology`).
- Resolve the target zone → its **coordinator IP** → send transport there.
- The ROOMS screen (§6) selects a zone; internally that resolves to a coordinator.

### Album art is plain HTTP (de-risks the S3 — no TLS needed for art)
Now-playing art is served by the **speaker itself** at `http://<zone-ip>:1400/getaa?...`
(plain HTTP, not HTTPS). So the art pipeline does **not** need `WiFiClientSecure`. Only
LRCLIB lyrics force HTTPS. Budget TLS memory for lyrics only, not art.

### Scope / auth reality check (Sonos S2)
Local SOAP control of **local** functions (transport, volume, queue, Sonos playlists,
radio/TuneIn favorites) works with no cloud auth. Streaming-service content (Spotify/Apple
surfaced as favorites) embeds service tokens in DIDL `res`+metadata; replaying stored
metadata generally works, but fresh service browsing can need account context.
→ **v1 scope:** Sonos playlists (`SQ:`) + radio/TuneIn favorites (`R:0`/`FV:2`). Deep
streaming-service browsing is out of scope for v1.

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
3. **PCF8574 button polling** for the press. Polling the expander at the UI tick can alias
   fast presses → **debounce / edge-latch** in software, or wire the PCF8574 **INT line** to
   a GPIO so presses are caught on edge, not sampled. Push/play-pause must feel crisp.
4. **SSDP discovery robustness across reboots.** Multicast M-SEARCH on ESP32 is flaky across
   routers/reboots. Mitigation: **persist discovered zone IPs + UUIDs in NVS**; on boot use
   the cached IPs directly and only re-run SSDP on failure (or periodic refresh).

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
- **HTTPClient** for SOAP + album art (both plain HTTP — art is served by the speaker at
  `http://<ip>:1400/getaa`). **WiFiClientSecure** only for LRCLIB lyrics (HTTPS).
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
- **Exit gate (hard):** sustained **flicker-free redraw while WiFi is connected/active**
  (see §2 perf note). If it tears, fix it here — it dictates the buffering strategy for all
  later UI work.

### Phase 1 — Talk to Sonos (1 day) — *recommended starting point*
- SSDP discovery → print discovered zone IPs.
- Minimal SOAP: send `Play` / `Pause`, read `GetTransportInfo`, against a hardcoded zone IP.
- Spike with **javos65 lib** first to validate your network/speakers fast, then decide
  build-vs-borrow for the real client (mind GPL if copying).
- **Dev affordance:** support a **hardcoded zone IP** (compile-time / NVS override) to
  bypass SSDP during development, and add a **serial SOAP tracer** (log request/response
  envelopes). Keep both for the life of the project — they pay for themselves in Phase 3.
- **Exit criteria:** the board can pause/resume real music on your Sonos.

### Phase 2 — Now Playing (2–3 days) — ✅ DONE (2026-06-25)
- Poll transport/position/volume; render title/artist/progress arc. ✅
- Inputs: twist→SetVolume, push→play/pause, touch prev/next. ✅
- Album art: fetch + `TJpg_Decoder` + downscale + **cache on track-change** (double-buffered). ✅
- Network off the UI task: `netTask` (poll + coalesced commands), `artTask` (decode). ✅
- **Gotchas hit & fixed on hardware** (all in git history):
  - Transport must target the **group coordinator**, not the speaker (volume is per-speaker).
    `coordinatorIpFor()` resolves it via `GetZoneGroupState`.
  - `TrackMetaData` is **double-escaped** — the art URL's `&` survived as `&amp;` until the
    field values were unescaped a second time.
  - Album art HTTP is **chunked**; reading the raw stream leaked chunk framing into the JPEG.
    Use `HTTPClient::writeToStream()` to de-chunk.
  - SSDP must do **multiple full rounds** (a single round misses speakers in a big household);
    pinned room (`SONOS_DEFAULT_ROOM`) only settles once that exact zone is discovered.

### Phase 3 — Browse & select (3–4 days) — *the big one*
- ContentDirectory `Browse` for `SQ:` (playlists) and `R:0`/`FV:2` (radio/favorites).
- Parse DIDL-Lite → scrollable LVGL lists.
- Select → (playlists: clear queue, add, Play index 0) / (radio: SetAVTransportURI + Play).

### Phase 4 — Polish (ongoing)
- ✅ **ROOMS zone switcher** — topology-based room list (deduped, alphabetical), long-press
  to open, twist=scroll, press=select, touch row to pick. (group/ungroup still TODO)
- ✅ **Clock screensaver** — 30s idle → dimmed NTP clock (TZ via `CLOCK_TZ`, default
  Pacific); any input wakes. Idle = min(encoder/button, touch) inactivity.
- ✅ **Settings persistence (NVS)** — picked room saved via Preferences; boot preference is
  saved room → `SONOS_DEFAULT_ROOM` → first discovered.
- ✅ **OTA updates** — ArduinoOTA as `sonos-nest` (`pio run -e ota -t upload`). NOTE: the
  build host must reach the device's LAN IP; from the current WSL PC the Sonos/ESP WiFi
  segment is unreachable (VLAN/client isolation), so OTA needs a host on that network.
- LRCLIB time-synced lyrics. (TODO)
- reconnect/error states, screen dim/sleep, brightness setting. (TODO)
- Optional: encoder acceleration, favorites radial, MQTT/HA.

### Effort note
Direct control is ~2–3× the proxy-approach firmware effort, almost all of it in Phase 3
(Browse + DIDL parsing). Payoff: a self-contained knob with no server dependency.

The per-phase day estimates above are **ideal-case** for someone fluent in LVGL + FreeRTOS.
First-time LVGL-on-RGB-panel work is lumpy: Phase 2 (LVGL render + FreeRTOS task split +
JPEG decode/cache) is realistically closer to a week than 2–3 days. Treat the numbers as
ordering/relative-weight, not a schedule.

---

## 8. Open questions / decisions for next session

### Decided
- **LVGL 9.x** (not 8.x). Rationale: SonosESP — the primary MIT reference to crib from —
  is 9.5; matching its version maximizes copy-paste leverage. Verify the ST7701 RGB driver
  is 9.x-compatible during Phase 0.
- **Polling, not GENA events**, for now-playing in v1 (events are a later optional upgrade).
- **Discovery: SSDP once → cache IPs/UUIDs in NVS → use cache on boot**, re-run SSDP only
  on failure (see §3 hard part #4).
- **v1 content scope:** Sonos playlists (`SQ:`) + radio/TuneIn favorites (`R:0`/`FV:2`);
  deep streaming-service browsing out of scope (see §3 auth note).

### Resolved during Phase 0
- **Elecrow board config pulled** from their official example repo
  (`CrowPanel-2.1inch-HMI-ESP32-Rotary-Display.../example/RotaryScreen_2_1/RotaryScreen_2_1.ino`).
  Full RGB pin map, 3-wire SPI init bus (CS=16/SCK=2/SDA=1), and ST7701 timings
  (hsync/vsync fp=10, pw=4, bp=20) are now in `src/board_pins.h`. Panel uses the stock
  `st7701_type5_init_operations`, BGR color order, IPS=false. PCLK/polarity left at
  Arduino_GFX defaults — tune empirically if it tears.
- **PCF8574 @ 0x21 confirmed**; P0=touch rst, P2=touch int, P3=LCD pwr, P4=LCD rst,
  P5=knob button (active-low). Matches the scaffold.
- **Touch is CST816 @ 0x15** (NOT GT911 as originally assumed) — `Adafruit_CST8XX`-class.
  Implemented as a minimal direct I2C driver in `src/hw/touch.cpp`.
- **Arduino_GFX pinned to 1.3.1** in `platformio.ini`. Elecrow's example uses the older
  API (`Arduino_ESP32RGBPanel(CS,SCK,SDA,DE,…)` + `Arduino_ST7701_RGBPanel`), dropped in
  upstream 1.4.x. `display.cpp` matches the 1.3.1 signatures verbatim (timings live on
  `Arduino_ST7701_RGBPanel`; only `gfx->begin()`, no `bus->begin()`). **Do not bump the
  GFX lib without rewriting `display.cpp`.**

### On-device verification — PASSED (2026-06-25)
Flashed `pio run -e bringup` to the real board and confirmed:
- **Display**: RGB test pattern correct (TL=red/TR=green/BL=blue/BR=white) — RGB pin map,
  ST7701 init, PSRAM framebuffer all good.
- **Touch (CST816 @0x15)**: coords across the panel, in 0..479.
- **Encoder (GPIO42/4)**: +1 per detent CW, -1 CCW (1 click = 1 step).
- **Button (PCF8574 P5)**: press pulls P5 low, debounced PRESS latches. NOTE: the K112 tact
  switch is stiff — needs a firm, centered push to actuate (initial "clicks" were the knob
  bottoming out, not the switch).
- Toolchain: ESP32 Arduino core 2.0.17, Arduino_GFX pinned to v1.3.1 (git tag).
- Flashing/serial from WSL2: see `docs/flashing-wsl.md` (usbipd `--auto-attach` required;
  each reset drops the bridge; `pio device monitor` needs a TTY so a pyserial reader is used).

### Still open
- [ ] Flicker-free-redraw gate under WiFi load — needs WiFi creds (Phase 1). Spinner UI
  renders; not yet stressed with concurrent network traffic.
- [ ] Default Sonos room/zone, and whether multi-zone/grouping is needed at v1.
- [ ] Build-vs-borrow the SOAP client (GPL javos65 lib vs MIT SonosESP patterns vs own).

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
