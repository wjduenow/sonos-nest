# sonos-nest — developer guide (read me first)

Firmware for **standalone physical Sonos controllers** — ESP32-S3 appliances that talk
**directly** to Sonos speakers over the local UPnP/SOAP API (no server, no cloud).
PlatformIO + Arduino + LVGL 9. One **shared core** drives multiple hardware **units**:

- **sonos-nest** (`nest` env) — the original: ELECROW CrowPanel 2.1" round rotary display
  (ST7701 480×480, EC11 encoder + knob, CST816 touch, PCF8574 expander).
- **sonos-sleep-machine** (`sleep-machine` env) — a nightstand sleep-sound player: rectangular
  2.8" Hosyond/LCDWIKI ES3C28P (ILI9341 240×320 SPI, FT6336 touch, microSD, ES8311 codec +
  speaker, mic, RGB-LED). **Working**: display, touch, SD, on-device MP3 playback, HTTP media
  server + remote SD management, a touch UX (home carousel, rooms, WiFi, track picker,
  settings, sleep timer), and **voice control** — three custom wake words drive the app hands-free
  (`boards/es3c28p/wake_word.cpp`; see the wake-word notes below). **Not yet wired**: the RGB-LED.
- **sonos-button** (`sleep-button` env) — **headless**: one button, an LED ring, no screen, on an
  ESP32-S3-CAM board (`boards/esp32s3cam/`). Press → the configured room starts the configured
  saved playlist, looped, at the configured volume; press again → stop. Configured from a web page
  on **:8080**, and readable over the **TCP log mirror on :2323** (it has no screen, so that is the
  only way to watch it — see the log-mirror section below). `core/unit.h` is LVGL-free, so a
  screenless unit is a first-class citizen and `app.cpp` needs no `#ifdef` for it; `-DHEADLESS`
  drops album art and slows the poll to one call every 3 s. Plan: `plans/04-sonos-button-plan.md`.
  > ⚠️ **It is the env that silently breaks.** `+<core/>` sweeps every core file into it, but its
  > `lib_deps` is overridden to just ArduinoJson — no LVGL, no TJpg. So any new core file touching
  > graphics breaks THIS env and only this env, and it is the one nobody builds by habit. That is
  > exactly how `art_cache.cpp` broke it for weeks (issue #7). **Two things now stop that
  > recurring, and both matter: every graphics-coupled core file lives in `core/ui/`, which this
  > env drops in one line (`-<core/ui/>`) — so put a new LVGL-touching file THERE, don't grow a
  > per-file exclusion list back; and CI builds all four app envs on every PR and every push to
  > `main`.** (A push to a side branch with no PR open is not covered — build it yourself.) The
  > convention is written up in `src/core/ui/README.md`.
- **sonos-jukebox** (`sonos-jukebox` env) — **a working wall-mounted landscape controller** on an
  ELECROW CrowPanel Advance 7" **ESP32-P4** (1024×600 MIPI-DSI EK79007, GT911 touch, dual speakers,
  ESP32-C6 for Wi-Fi over SDIO/ESP-Hosted). **Working**: panel, LVGL 9 + touch, Wi-Fi, zone
  discovery/switching, transport, album art, OTA, portal registration, UI click feedback on the
  onboard speakers, and four screens (Now Playing · Radio · Rooms · Settings). **Rooms does
  grouping**: a checkbox per room joins/leaves the active group, plus UNGROUP, per-room volume
  (±5) and per-room play/pause, over a group summary bar. Live per-room volume + play state come
  from **`core/room_status.{h,cpp}`**, polled by netTask one room per step and ONLY while the page
  asks (`keepAlive()`), so it costs nothing when you are elsewhere. `core/` runs
  **unmodified** — Arduino 3.x needed one shim in `net/registrar.cpp`. The **rotary dial works**:
  an Arduino Modulino Knob on the **shared I2C bus via J13** (not the 11-pin GPIO header), twist =
  volume and press = play/pause from any page — `boards/crowpanel_p4_7in/knob.cpp`.
  > ⚠️ **Rooms is latency-bound by SOAP, so the fixes are all about NOT waiting and NOT blanking.**
  > Three things were each worth a visible second. (1) A fixed poll gate made the first fill 3.6 s
  > when the calls themselves cost ~150 ms/room — it now steps fast until one full round completes,
  > then backs off to 400 ms. The switch is on "a round finished", NOT "every room has a reading":
  > an off speaker never answers and would pin it fast forever. (2) A `roomstatus::invalidate()`
  > after each grouping op blanked all nine rooms to `--` and refilled — deleted, because volume is
  > unaffected by grouping and transport is re-derived from the new coordinator. (3) A checkbox
  > cannot be confirmed until netTask runs the SOAP op **and** a full `ssdpDiscover()` topology
  > re-read, so every optimistic edit (volume, transport, group membership) is held over the poller
  > for a few seconds or the UI snaps back and the tap reads as ignored. The `g_zonesGen` rebuild
  > is what ends a group hold — that bump IS the confirmation.
  > ⚠️ **`soapAction`'s keep-alive only helps for consecutive calls to the SAME host**
  > (one static `WiFiClient`, `setReuse(true)`). Anything that round-robins speakers — the Rooms
  > poller — pays a fresh TCP connect per step over the SDIO bridge. Ordering calls so a room's
  > volume and transport go back-to-back is why they share one connection.
  > ⚠️ **It answers at 7-bit `0x3A`, NOT the `0x76` its datasheet advertises** — those are 8-bit
  > addresses and Arduino's `Wire` is 7-bit (`0x74 >> 1 == 0x3A`, and the pinstrap byte it returns
  > is literally `0x74`). Probing only the documented values found nothing and the driver reported
  > "no dial on the bus" with the dial plugged in and working. Expect the same off-by-a-shift for
  > the PCF8574.
  > ⚠️ **A NACKed `requestFrom` returns the STALE RX BUFFER, not an error** — 4 "readable" bytes
  > that are a copy of the last real reply (or zeros on a cold bus). Byte content is never proof a
  > device is there; the ACK is. This invented a phantom dial on an empty bus.
  > `GET /api/knob` dumps driver state + a live probe of every candidate address; the full bus
  > census runs at boot behind `KNOB_DEBUG` because an ACK probe to an *absent* address blocks
  > ~80 ms, so sweeping the range takes ~9 s — far too slow for an HTTP handler.
  **Not done**: the 4 transport buttons (a PCF8574, same bus) and the case.
  > ⚠️ **The Amazon crawl must never restart from zero, and its tree must contain nothing else.**
  > Two bugs kept this device in a permanent reboot loop. (1) `amazon::post()` skipped HTTP headers
  > with `readStringUntil()`, and `Stream::timedRead()` is a **busy-wait with no yield** — with a
  > 15 s timeout it starved IDLE0 and the task watchdog aborted the chip mid-crawl (`CPU 0:
  > radiocache`). Never use a Stream helper on a TLS socket here; read blocks and yield. Phase
  > timing will NOT find it (every phase is <1.5 s) — decode the register dump. (2) The artwork
  > cache lived at `radio/art`, **inside the tree `refresh()` rmTree+renames**, so `rmdir` failed,
  > the swap failed, and no crawl ever published however often it succeeded. Art now lives at
  > `radioart`, a sibling, and `rmTree` recurses. The crawl is now **resumable across reboots**
  > (per-genre files + a `genres.tsv` manifest), and `post()` holds **one keep-alive TLS session**
  > instead of 27 connect/handshake/close cycles.
  > ⚠️ **One unresolved fault: the ESP-Hosted link dies under load** (`rssi=0` while `wifi=3`).
  > Recovered automatically by reboot, not cured — matches upstream esp-hosted-mcu #167/#121.
  > **Never "fix" it by re-initialising the transport**: `esp_hosted_deinit()` under live lwIP
  > users hard-freezes the device. Next leads are a slower SDIO clock and the C6 firmware upgrade.
  > ⚠️ **Power-cycle after every upload**, and read the `[health]` heartbeat before diagnosing any
  > "hang" — it prints from `uiTick`, so its *absence* means the UI task is stuck (suspect the LVGL
  > pool, ~1 KB per list row) while its *presence* with `zones=0` means the link died. Two very
  > different faults, identical on screen. The same numbers are on the wire without a serial cable:
  > **`GET /api/config` → `.health`** (`core/webconfig.cpp`) carries heap/PSRAM/LVGL/SOAP counters.
  > ⚠️ **Know which resource you are actually spending — they are nowhere near equal.** Measured on
  > hardware: **PSRAM 32 MB, ~29.6 MB free** (frame buffer 1.17 MB + LVGL pool 512 KB + album art
  > 410 KB + a 512 KB JPEG staging buffer + station tile cache 170 KB ≈ 2.8 MB); **app slot
  > 6.25 MB, ~4.3 MB free** (and the
  > 3.375 MB `spiffs` partition is *unused* — nothing mounts it — if you ever need more);
  > **internal SRAM is the only tight one.** ~115 KB free but the `heapLargest` block is only
  > ~32-49 KB, and `core/amazon.cpp` notes the crawl runs at **~40 KB free**. New buffers go in
  > PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) — watch `heapLargest`, not just `heapFree`,
  > because on the nest it was *fragmentation* that starved LWIP and surfaced as Sonos
  > "connection refused". The 512 KB LVGL pool peaks at only ~48 KB (`lvMemMax`) so far.
  > ⚠️ **`ART_MAX_PX` is a ceiling the decoder UNDERSHOOTS, not a target.** TJpgDec scales only by
  > powers of two, so decoded size is source/(1,2,4,8) — the largest that fits under the cap. Sonos
  > serves 640 px covers, so a cap of **280 yielded 160 px art**: a cap 100 px larger than the tile
  > still produced art at well under half of it, and everything looked soft on a device with 30 MB
  > of spare PSRAM. It is **320** here for exactly that reason (640/2). Pick a cap by asking what
  > `source/2^n` lands on, not how big you want it.
  > ⚠️ **The screensaver OWNS THE BACKLIGHT (plans/10; the block near the bottom of the unit's
  > `screens.cpp`).** Awake / Showing / Blank off LVGL's inactivity timer — so `backlightSet()`
  > belongs to `saverTick()` now, and calling it from anywhere else just gets overwritten on the
  > next tick. Three non-obvious things. **The dial is not an LVGL input device** (it is polled over
  > I2C), so `handleDial()` calls `lv_display_trigger_activity()` or a panel being actively turned
  > falls asleep under your hand. **The overlay stays up while blanked even with the screensaver set
  > to Off** — that is what eats the wake-up tap so it cannot press an invisible button. And the
  > 120 px clock is **a real font** (`lv_font_clock_120.c`), not a scaled label: scaling allocates a
  > ~240 KB ARGB draw layer per repaint from the 512 KB pool, and pool exhaustion here is a UI
  > freeze, not a dropped frame. That file's header has the regeneration command.
  > ⚠️ **Now Playing is EVENT-DRIVEN here (`core/sonos/gena.*`, `-DGENA_EVENTS`, plans/09).** Sonos
  > pushes state; the poll drops to a 15 s backstop while eventing is trusted (3.00 SOAP calls/sec
  > → 0.09). Two things to know before touching it. The backstop is the **same poll, just slower** —
  > it was briefly reduced to position-only and Now Playing went blank, because nothing else could
  > repopulate the title. And **trust is revocable**: subscribed AND at least one event received,
  > else it returns to 1 Hz by itself.
  > ⚠️ **Eventing gave several fields a SECOND writer, and that is where nearly every bug came
  > from — the new writer overwriting something better than it had.** Volume mid-turn, album art on
  > pause, track identity from container metadata, relative art URLs the poll had always fixed up.
  > **The rule: a partial or lower-quality update must never erase what a fuller one established.**
  > `playerApplyTrack()` / `playerVolumeHeld()` in `player_state.h` encode it — use them rather
  > than assigning now-playing fields directly, from either the poll or an event.
  > ⚠️ **Don't guess which subsystem is eating the heap — `core/heap_watch.h` will tell you.**
  > `heapwatch::note("tag")` records the tag owning the internal-heap low-water; read it back as
  > **`health.heapLow`** in `/api/config`. Put notes *after* an allocation while it is still held.
  > It has already overturned one confident guess: GENA's NOTIFY buffering, the obvious suspect,
  > bottoms out at ~113 KB free (~4 KB) and was never the problem. If `heapMin` is lower than any
  > recorded tag, the cause is in code you have not tagged — that gap is the clue, not noise.
  > ⚠️ **mbedTLS allocates from PSRAM here, and that is load-bearing — don't revert it.** The stock
  > config is `MBEDTLS_INTERNAL_MEM_ALLOC` with `SSL_MAX_CONTENT_LEN=16384`, i.e. a 16 KB in + 16 KB
  > out buffer per TLS connection taken from the one resource this board lacks. Measured: the
  > Amazon crawl went 102 KB → 45 KB free *before reading a byte*, then to **min 9,828 B / largest
  > 14,324** — through the LWIP floor — and the device rebooted half way through the genres, every
  > night. `jukebox_base`'s `custom_sdkconfig` now sets `MBEDTLS_EXTERNAL_MEM_ALLOC=y` plus
  > `ASYMMETRIC_CONTENT_LEN` (out 4 KB): same crawl now **completes all 26 genres / 1055 stations
  > in one pass at min 50,536**. Verify a config change actually applied by grepping the generated
  > `sdkconfig.sonos-jukebox`, not the ini — a `custom_sdkconfig` line that is ignored fails
  > silently and you will be measuring nothing.
  > ⚠️ **microSD (slot 0; the C6 is slot 1, so they don't share a bus).** Pins CLK 43 / CMD 44 /
  > D0 39, 1-bit @ 10 MHz, no LDO power handle. **Never use Arduino's `SD_MMC`** — it takes its pins
  > and a power-enable pin from the board variant, whose stock `BOARD_SDMMC_POWER_PIN 45` is this
  > board's **I2C SDA** (it would kill touch). Use the IDF `sdmmc` API. And FATFS **must** be
  > `CONFIG_FATFS_SECTOR_512` — the inherited default was `SECTOR_4096` (a SPI-flash option), which
  > makes every FatFs LBA 8x wrong and shows up as `sdmmc_write_blocks failed (0x107)` **timeouts**
  > that look exactly like bad hardware. Symptom to recognise: **raw sector I/O flawless, everything
  > through `fopen`/`fwrite` timing out.**
  The screen UI + case design system is in-tree as the **`/sonos-jukebox-design`** skill.
  **Read `plans/07-sonos-jukebox.md` before touching this** — it is different silicon (RISC-V) on a
  different toolchain, and several failure modes here are silent.
  > ⚠️ The jukebox envs use the **pioarduino** fork of platform-espressif32, which publishes
  > itself under the name `espressif32` too. `[env]` pins `platform = espressif32@6.9.0` to keep
  > the S3 units on Arduino 2.0.17; installing the fork without that pin silently retargets
  > `nest` and `sleep-machine` to Arduino 3.x. **Don't loosen either pin.**

Units share all Sonos control/discovery/browse/settings/net/OTA; they differ only in
`src/boards/<board>/` (drivers) and `src/units/<unit>/` (UX). See **Architecture** below.

- Full plan + feature scorecard + history: **`plans/01-sonos-knob-controller-plan.md`**
- Multi-unit reorg rationale + layout: **`plans/02-multi-unit-reorg.html`**
- New form factor (jukebox) + design system: **`plans/07-sonos-jukebox.md`**
- Jukebox screensaver — what shipped, and the video/photo options that did not:
  **`plans/10-jukebox-screensaver.md`**. Read §1 before proposing anything that moves pixels: it
  has the measured budget, and the two facts that kill the obvious ideas — the P4's H.264 block is
  **encode-only**, and the SD card is 1-bit at 10 MHz with D1-D3 unwired, so video is bound by the
  card, not the decoder.
- Music services + the Radio feature: **`plans/08-music-service-integration.md`** — Part 1 is the
  feature **as built** (Favorites + a real Radio page over Amazon Prime Stations, both backed by SD
  caches with artwork, A-Z jump, search and scroll detents); Part 2 is the research record. Read
  Part 1's *four things that would have broken it* before touching the artwork or token paths.
  **OAuth services (YouTube Music, Spotify accounts) cannot be browsed** — closed question, don't
  re-open: the favourite id is an opaque account-scoped token, the household's OAuth token is on the
  player but write-only, and the cloud Control API has no browse path. Favourites (`FV:2`) are the
  only route for those, already implemented.
  **But `Auth="Anonymous"` services CAN be browsed on-device** — 32 of 106 here (TuneIn, SomaFM, NTS,
  Radio France…). Verified by running it: an empty `<credentials/>` SOAP header is the entire
  requirement, and `getMediaURI` resolves a station to a stream URL anonymously too. One playback
  test remains. **Spotify tracks/albums/playlists are constructible too** (its Sonos id is a
  transparent wrapper) — but its *stations and mixes* are gone at the Spotify end, not the Sonos end.
  **DeviceLink services CAN be browsed in full — PROVEN on Amazon Music.** 15 of 106 here are
  DeviceLink (vs 59 AppLink, 32 Anonymous). One browser authorisation by the owner yields an
  authToken/privateKey, and `getMetadata` then returns the whole tree — **"Prime Stations" is a
  root-level container, 26 genres x ~50 stations**, no Sonos app or cloud involved. The `#chunk-`
  suffix in a station id is **minted per response — never construct one**, just take the browsed id
  verbatim (old ones stay valid indefinitely). The `prime/stations/` *path* is legacy; build against
  `catalog/stations/`. Gotcha: `linkDeviceId` is per-request and required to redeem the code — drop
  it and the user has to authorise again.
  Handy trick recorded there: `http://<speaker>:1400/getaa?s=1&u=<encoded URI>` is a **read-only
  oracle for URI validity** — 200 = real, 404 = not — and it also gives album art for free. It covers
  tracks only: for containers/stations a 404 means nothing. Also records two durability risks to this project's premise (`customsd.htm` now 403s
  on S2; a Connection Security toggle can now require auth on the **LAN** APIs).
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

> ⚠️ **Which env you pass to `-e` rewrites shared global state — PlatformIO packages live in
> `~/.platformio`, NOT in the project, and are shared across every checkout and worktree.** The
> jukebox envs are ESP32-P4 and pull the pioarduino framework, which installs over the *same*
> `packages/framework-arduinoespressif32` directory the S3 envs use. So `pio run -e sonos-jukebox`
> leaves Arduino 3.3.11 there, and the next `pio run -e nest` fails with **`Implicit dependency
> 'FreeRTOS.h' not found`** or **`esp_timer.h: No such file`** — errors that name the framework,
> never the env you actually built. It swaps back the same way. **The fix is
> `pio pkg install -e <env>` for the env you want**, then rebuild.
> Two consequences: **two Claude sessions must not build different silicon at the same time** (the
> loser sees a build break with no cause anywhere in its own diff), and a green `nest` build proves
> nothing about a jukebox build you ran an hour ago. CI is immune — every matrix job is a fresh
> runner, and the PlatformIO cache is keyed per env precisely so the two platforms never share one.

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
- `core/ui/` — **the only LVGL/TJpg-coupled part of core**, kept in its own subtree so headless
  envs exclude it wholesale (`-<core/ui/>`) instead of maintaining a per-file list. Members:
  `album_art.{h,cpp}` (art fetch + TJpg decode → LVGL image, form-factor-agnostic) and
  `art_cache.{h,cpp}` (the jukebox tile cache). Callers in device-agnostic core (`app.cpp`,
  `webconfig.cpp`) guard include *and* call sites with `#ifndef HEADLESS`. See its README.
- `core/net/` — `wifi`, `ota` (OTA hostname = `DEVICE_HOSTNAME` macro, set per env), `logmirror`
  (see below).
- `core/board.h` / `core/unit.h` — the HAL + UX contracts.

### Reading a running device: the TCP log mirror (`core/net/logmirror.{h,cpp}`)

`LOG` is a drop-in for `Serial` that also writes to anyone connected on **TCP :2323**
(`nc <ip> 2323`). Non-blocking by construction — writes go to an 8 KB ring drained by its own
task, and overflow drops the *oldest* bytes and counts them, because a diagnostic that can stall
its caller is how you freeze the UI task with the very thing meant to explain the freeze.

**Which units have it, and why — the numbers are measured free / minimum-ever internal heap:**

| unit | heap | mirror | why |
|---|---|---|---|
| **sonos-jukebox** | 98 / 74 KB | **yes** | wall-mounted; rear port is power-only, so this and OTA are the only ways in |
| **sleep-button** | 243 / 226 KB | **yes** | **headless** — no screen at all, so without a cable it is unobservable; also the most headroom of any unit |
| sonos-nest | 78 / 60 KB | no | has a screen showing its own state; `heapLargest` still unknown on it (pre-`ccfe157` firmware). Viable later — read that first |
| sleep-machine | 30 / **14.5 KB** | **no** | 14.5 KB min is already the range where LWIP cannot get socket buffers and the symptom is Sonos **`connection refused`**. Also has a screen and sits within cable reach |

> ⚠️ **Enabling it is TWO things, and the flag is only one of them.** Add `-DLOG_MIRROR` to the env
> *and* call `logMirrorBegin()` from that unit's `uiInit()`. The flag alone compiles the module in
> and nothing ever starts the listener.
> ⚠️ **It tees `LOG`, never `Serial`.** Any file still calling `Serial.print*` is invisible to a
> remote reader — which on a headless unit means invisible full stop. `core/` and the units that
> enable this use `LOG` throughout; bring-up/test sources deliberately keep `Serial`, because those
> run with a cable attached anyway.
> ⚠️ **Put the include ABOVE any `#if __has_include("secrets.h")` block.** Inside one, `LOG` is only
> defined on machines that happen to have the gitignored `include/secrets.h` — so it builds for you
> and fails for everyone else. Cost real time once already; a fresh worktree has no `secrets.h`.

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
- **…and `CurrentURIMetaData` is escaped a THIRD time.** Escape depth is not uniform across
  fields: `TrackMetaData` arrives as `&lt;DIDL-Lite`, but `GetMediaInfo`'s `CurrentURIMetaData`
  arrives as `&amp;lt;DIDL-Lite`. Feed the latter to `parseNowPlaying()` unchanged and it finds no
  `<dc:title>` and returns **nothing, with no error** — a permanently blank Now Playing. Verified
  on hardware. `getMediaInfo()` normalises it with one extra `xmlUnescape()` so callers can treat
  both the same; don't "simplify" that away. **Count the layers on any new field before trusting it.**
- **Now Playing needs a fallback source — `TrackMetaData` is a STUB for content playing outside
  the queue.** A direct Spotify track reports `item id="-1"` with no `dc:title`, no `dc:creator`
  and no art, while the speaker plays it perfectly happily; the real title is in
  `GetMediaInfo`'s `CurrentURIMetaData` (poll path) and `AVTransportURIMetaData` (GENA event).
  Symptom to recognise: **audio is fine, position advances, the screen says "Nothing playing".**
  Both paths fall back automatically now, and the poll only pays the extra call when the primary
  source came up empty.
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
- **Wake word (microWakeWord) — live in the app, 2 of 3 phrases.** Full story + the "Kinder Rise"
  next step: **`plans/03-wake-word-integration.md`**. `boards/es3c28p/wake_word.cpp` is the real
  engine (`sleep-machine`); `wake_test.cpp` / `sleep-machine-wake` is the standalone bring-up. The
  board only reports **which phrase it heard** (`wakeWord*` in `core/board.h`); the unit decides what
  that means (`handleWakeWord()` in `units/sleep_machine/screens.cpp`), since boards must not touch
  `g_pending`/`settings`. Each phrase calls the same callback as its on-screen button, so voice and
  touch can't drift: **Bedtime** → the **Sonos Sleep playlist** (`cloudCb`, *not* the SD-stream
  card), **Wake-Up** → stop. **"Kinder Rise and Shine" is vendored but NOT shipped** — it scores
  0.00 on most real utterances while the other two hit 0.96-1.00 on the same audio, and a lower
  cutoff doesn't help (the failures are zeros, not near-misses). Set `WAKE_DEBUG 1` in
  `wake_word.cpp` for a 2 s heartbeat (mic rms + per-model peak) — it's the tool for tuning a new
  phrase, and it distinguishes "scored 0.7, retune" from "scored 0.00, retrain".
  Both run mic → TFLM microfrontend (40-ch log-mel, 30 ms/10 ms,
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
- **Wake word: all 3 models run concurrently thanks to esp-nn (~4 ms/inference).** With TFLM's
  *reference* kernels each inference is ~17 ms, and a 3-frame feature window arrives every 30 ms, so
  3-up cost 51 ms/30 ms = 170% of real-time → the feature queue overflowed (`drops` climbed) and the
  models saw discontinuous audio. **esp-nn** (Espressif's SIMD kernels, vendored at
  `lib/tflm/esp-nn`) cuts that to ~4 ms → 3-up is ~40% of real-time, `drops=0`. It lives *inside*
  lib/tflm on purpose: nothing in `src/` includes `esp_nn.h`, so PlatformIO's LDF would never build
  it as a standalone lib. Needs `-DESP_NN -DCONFIG_NN_OPTIMIZED -DCONFIG_IDF_TARGET_ESP32S3
  -mlongcalls`, and the 7 reference kernels it replaces (add/conv/depthwise_conv/fully_connected/
  mul/pooling/softmax) must be excluded from the build or they collide. `kOnlyModel` in
  `wake_test.cpp` still selects a single model for isolating tests (-1 = run all three).
- **Any `src/` file including a TFLM header MUST repeat lib/tflm's flags in its env**, at minimum
  `-DTF_LITE_STATIC_MEMORY` (plus `-fno-exceptions`). `library.json`'s `build.flags` configure only
  the library's OWN sources — they do not propagate to `src/`. `TF_LITE_STATIC_MEMORY` selects a
  **different `TfLiteTensor` field order**, so a mismatch is not a link error: `AllocateTensors()`
  returns OK, `interp->input(0)` returns a sane-looking pointer, and every field read through it is
  garbage (`params.scale=-nan`, `dims->data[1]`=poison) → `StoreProhibited` on the first inference.
  The `sleep-machine-wake` env had these; adding wake_word.cpp to `sleep-machine` without them cost
  hours. Symptom to recognise: valid pointers + nonsense quant params = ABI mismatch, not memory.
- **Wake word costs ~31 KB of internal SRAM, and internal SRAM is what breaks Sonos first.** The
  models are nearly free (~228 B each; arenas are PSRAM) — the cost is I2S buffers and task stacks.
  `buffer_count=8/buffer_size=1024` cost **33.8 KB** (audio-tools allocates well past the DMA
  descriptors); `4/512` costs 9 KB and detection is unchanged (0.96-0.99). Stacks are sized off
  measured high-water marks. When internal free fell to ~15 KB (largest block 7.6 KB), LWIP couldn't
  get socket buffers and the **symptom was Sonos `connection refused` + "File system is not
  mounted"** — nothing pointing at the wake word. Healthy is ~47 KB free / ~35 KB largest.
- **Wake tasks are pinned to core 1 — core 0 belongs to the network.** netTask (prio 2) and
  media-httpd (prio 1, streams SD → Sonos) live on core 0; the wake capture task must be high-prio
  (or I2S goes stale), so on core 0 it starves them. That failure only appears **while streaming** —
  an idle device polls Sonos fine, so a "does the network work?" control test that isn't playing
  anything proves nothing. Always test the *streaming* path.
- **A corrupt incremental build looks like "every model silently scores 0.00"** — not a compile
  error. This machine has a known hardware fault (BIOS update pending; random SIGKILL/ICE under
  load). If detection dies after a change that couldn't affect it, **`pio run -t clean` before
  debugging the code** — a clean rebuild of identical source restored it. Build with `-j 2`.
- **Wake-word testing: the mic needs LOUD, close speech (pcmRms >~10000) to fire.** At pcmRms ~4000
  *nothing* fires — with any kernel set. Several hours were lost concluding "esp-nn miscomputes"
  from tests that were really just too quiet. Always check `pcmRms` in the heartbeat before
  believing a negative result.
- **ES8311 mic (es3c28p) needs an explicit ADC power-up — `arduino-audio-driver` only inits for
  playback.** Setting I2S RX + ADC volume/gain yields a DC-centered dither floor that ignores
  sound; you must also power the analog ADC (`REG0E=0x02`, its suspend value `0xFF`=off), enable
  the ADC clock (`REG01=0x3F`), route ADC data (`REG44=0x50`), unmute ADC onto SDOUT, select the
  analog mic (`REG14=0x1A`), and give it real gain (low-output mic — analog PGA `REG16` near max
  is best SNR). Full sequence + a live level meter: `mic_test.cpp` / the `sleep-machine-mic` env.

## secrets.h (gitignored — `include/secrets.h`)
All optional. `WIFI_SSID`/`WIFI_PASS` bake WiFi in at flash time; leave them unset and **every**
unit provisions on first boot via the SoftAP captive portal (`core/net/portal.cpp`, AP
`<hostname>-setup`) — screened units overlay a "join <AP>" message (`uiProvisioning()`), the
button is headless. Hold the knob/button through power-on to re-provision. Also optional:
`SONOS_DEFAULT_ROOM`, `CLOCK_TZ` (POSIX), `OTA_PASSWORD`, `SONOS_ZONE_IP` (dev bypass). Template:
`include/secrets.example.h`.

## Conventions
- Commit/push only when asked. Branch `main`, remote `origin` (github.com/wjduenow/sonos-nest).
- Keep `hardware/` commits separate from firmware — the user owns that work. Layout:
  `hardware/round-nest-2.8/` (original round CrowPanel unit: `wall/` mount) and
  `hardware/rec-2.8/` (ES3C28P rectangular board: `countertop/` nightstand stand).
- Test loop: build → flash (USB or `/ota`) → user confirms on device → commit + push.
