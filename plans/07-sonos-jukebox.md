# 07 — sonos-jukebox (third form factor)

**Board chosen:** ELECROW **CrowPanel Advance 7" ESP32-P4 HMI AI Display** (DHE04107D) —
1024×600 IPS, MIPI-DSI, GT911 touch, dual speakers, camera header.
**Status:** design system imported. **P4 toolchain proven end-to-end — `pio run -e jukebox-bringup`
builds and links a flashable image.** Screen bring-up is at stage 1 (not yet flashed to hardware).
**Current focus: the screen — panel is up, LVGL + touch is next.** Buttons/knob are deliberately deferred (see *Physical
controls* — they are not on this board at all).

A wall-mounted Sonos controller: a large landscape touchscreen with a physical control column to
its right — a push-to-select rotary dial over a 2×2 grid of momentary caps (play/pause · skip
forward · skip back · change rooms). Matte-white printed case, flush wall plate, rear USB-C.

## Where the design lives

`.claude/skills/sonos-jukebox-design/` — the full design system, imported from the Claude Design
project `e55a4165-1f93-46dd-816c-66c679c9a2e6`, registered as the user-invocable
**`/sonos-jukebox-design`** skill so it loads on demand instead of sitting in every context.

- `tokens/hardware.css` — the case's dimension source of truth (mm).
- `industrial/` — front elevation, side section (wall mount + rear USB-C), exploded stack,
  control-layout spec, accent trim variations. CSS technical drawings; open in a browser.
- `tokens/` + `guidelines/` — near-black `--screen-bg #0e0f12`, single accent
  (**amber `#e8892b`**, teal/coral as alternates), Hanken Grotesk + JetBrains Mono, 4px grid.
- `components/` + `ui_kits/jukebox-screen/` — React/HTML **specification** of the screen UI.
  Not shippable code; the device is LVGL. See the translation note in the skill's `SKILL.md`.

## The board

Manuals, course PDFs and the P4 datasheet: `docs/crowpanel-advance-p4-7in/`.
Pin map (from Elecrow's `board_config.h`): `src/boards/crowpanel_p4_7in/pins.h`.

| | |
|---|---|
| SoC | **ESP32-P4NRW32** — RISC-V dual-core HP @ 360/400 MHz, LP core @ 40 MHz |
| Memory | 16 MB flash, **32 MB in-package PSRAM**, **768 KB L2MEM** internal |
| Display | 1024×600 IPS, **MIPI-DSI**, **EK79007** driver IC, active area 155×87 mm |
| Touch | **GT911** I2C @ 0x5D (INT low at reset) / 0x14 |
| Audio | **NS4168** class-D amp → **two onboard speakers**, I2S; separate PDM mic |
| Wireless | **ESP32-C6-MINI-1** (Wi-Fi 6 + BLE 5.3) on a swappable header, **SDIO + ESP-Hosted** |
| Other | microSD, camera header (MIPI-CSI), 11-pin GPIO header, Crowtail I2C/UART, 2× USB-C |
| Physical | PCB 180 × 105 mm, 5 V / 2 A |

### What this buys us

This is a much better fit for the design than an ESP32-S3 would have been:

- **768 KB internal L2MEM** vs the S3's ~150 KB free. Internal SRAM is *the* recurring failure mode
  in this repo — it is why nest OTA is unreliable and why the wake word starved LWIP into
  "connection refused". That pressure largely goes away here.
- **MIPI-DSI with a real display controller**, instead of streaming a framebuffer out of PSRAM over
  RGB-parallel. 1024×600 would have been marginal-to-impossible on an S3; on the P4 it is the
  intended use case.
- **Hardware JPEG codec + 2D-DMA + PPA.** Album art currently decodes in software via TJpg_Decoder;
  the P4 can do it in hardware, and PPA can accelerate LVGL blits.
- 32 MB PSRAM makes double/triple framebuffering (tear-free mode) affordable.

### What it costs us — read before writing any code

**1. Different silicon ⇒ different toolchain.** The P4 is RISC-V and the official
`platform = espressif32` does not support it at *any* version. The jukebox envs use the
**pioarduino** fork (`55.03.311` = Arduino core 3.3.11 / IDF 5.5.5).

> ⚠️ The fork publishes itself under the name **`espressif32`** too. Installing it immediately
> retargeted `nest` and `sleep-machine` to Arduino 3.x — verified, then fixed by pinning
> `platform = espressif32@6.9.0` in `[env]` (6.9.0 is the last platform on
> framework-arduinoespressif32 ~3.20017, which the S3 units are written against).
> **Both pins are load-bearing. Don't loosen either.** `pio run -e nest` was re-verified green
> after the pin.

**2. `src/core/` is not yet known to compile under Arduino 3.x.** Everything shared —
`soap_client`, `ssdp`, `library`, `settings`, `webconfig`, `album_art`, `net/*` — was written
against Arduino 2.0.17 / IDF 4.4. Expect friction in `WiFi`, `WebServer`, `HTTPClient` and
`Preferences`. This is why the bring-up env excludes `core/` entirely: screen first, port second.

**3. The P4 has no radio — Wi-Fi is the C6 over SDIO, and it WORKS. ✅ RESOLVED ON HARDWARE.**

The multicast probe now passes **6/6** on the real board:

```
[PASS] associate            ip=192.168.68.x
[PASS] dns lookup
[PASS] M-SEARCH :1900       16 responder(s)
[PASS] M-SEARCH ephemeral   18 responder(s)
[PASS] multicast group join join=ok notifies=1
[PASS] HTTP to speaker      HTTP/1.1 200 OK
VERDICT: SSDP discovery works over ESP-Hosted.
```

**`core/sonos/ssdp.cpp` should port across unchanged** — including its `udp.begin(1900)` fixed
source port, which was the case I was most worried about. Inbound multicast (group join +
NOTIFY) works too, so a NOTIFY-based discovery fallback is available if ever wanted.

Getting there took two fixes, and the first was the whole problem:

1. **SDIO wiring.** The initial run failed with `sdmmc_send_cmd returned 0x109` (timeout) and
   never associated, because the build used a stock `esp32-p4*` profile whose ESP-Hosted
   defaults are Espressif's EV board. Elecrow wires the C6 differently, and **the 7" differs
   from the 5"**: 1-bit bus @10 MHz on CLK=18/CMD=19/D0=14/D1=15, slave reset GPIO32 active-high,
   1500 ms. (The 5" is 4-bit on GPIO49–54, reset GPIO20.) That config now lives in
   `[jukebox_base] custom_sdkconfig`, with the board definition in `boards/crowpanel-p4-7in.json`.
   Wrong pins do not fail loudly — the board boots and Arduino starts ESP-Hosted before the
   transport dies, which reads like broken hardware.
2. **`ARDUINO_USB_CDC_ON_BOOT=1` had to go.** Serial on this board is the CH340K UART bridge,
   not native USB CDC. With `custom_sdkconfig` the build switches to ESP-IDF mode, where that
   flag breaks the compile outright (`HardwareSerial.h: 'USBSerial' was not declared`).

**C6 co-processor firmware: still 2.3.0 — the update did not take.** Using Elecrow's linked
project (`crowpanel-advanced-p4-c6-upgrade`, target `crowpanel-p4-70-90-101`) the transfer
succeeded but activation did not:

```
[PASS] Connected to C6 slave in 1886ms
[PASS] OTA transfer completed in 14153ms
[FAIL] Activate failed: ESP_ERR_NOT_SUPPORTED (0x106)
[DIAG] C6 version after OTA: 2.3.0
```

The shipped 2.3.0 slave is too old to support the activate RPC, so the image was written but
never marked bootable. **This is not currently blocking anything** — everything above passes
with 2.3.0 — but the host stack (2.12.x) does warn that a version gap can cause RPC timeouts, so
it is a latent stability risk worth closing. Next avenue: flash the C6 directly over its own
UART rather than via SDIO OTA; Elecrow ships a second guide for the ESP-IDF route
(`docs/crowpanel-advance-p4-7in/c6-upgrade/`, fetched by `fetch-docs.sh`).

Note `custom_sdkconfig` makes PlatformIO build this as an IDF project. It prints
"the 'src_filter' option cannot be used with ESP-IDF" — **that warning is cosmetic here**;
`build_src_filter` was verified to still select our sources correctly.

**4. Two build-system landmines, both already hit and worked around.**

- *SCons.* The IDF 5.5 include list pushes compile commands past SCons' default
  `MAXLINELENGTH`, into a response-file path that is broken in the bundled SCons 4.8.1
  (`AttributeError: 'CmdStringHolder' object has no attribute 'data'`). It surfaces as a
  per-object-file failure, so it reads like a code error. Fixed by
  `tools/p4_maxlinelength.py`, wired into the jukebox envs.
- *LVGL.* With `lvgl` resolving to 9.5.0 this env fails to compile LVGL's generated
  `widgets/property/lv_span_properties.c` — "#endif without #if" against
  `lv_conf_internal.h`. **This is P4-specific, not an LVGL regression**: `nest` was
  clean-rebuilt from scratch against the same 9.5.0 and succeeded (709 files, unchanged
  size). The likely cause is `include/lv_conf.h` being an Xtensa/S3 file (it pins
  `LV_USE_DRAW_SW_ASM=NONE` and its comments assume the S3) meeting 9.5.0's RISC-V paths.
  Stage 1 sidesteps it by not depending on LVGL at all; **resolve it before stage 3** —
  probably a board-specific `lv_conf`, or pinning lvgl.

**5. Switching envs re-downloads the framework.** The P4 framework (3.3.11) and the S3
framework (3.20017) install to the *same* `framework-arduinoespressif32` package directory,
so alternating `pio run -e nest` and `pio run -e jukebox-bringup` re-fetches ~78 MB each way.
Annoying, not dangerous. The 2.1 GB `framework-arduinoespressif32-libs` is P4-only and stays.

**6. Elecrow's examples are LVGL 8.3.11**; this repo is on LVGL 9. Their `lvgl_v8_port.cpp` is not
reusable as-is. Keep LVGL 9 (the core and both existing units depend on it) and write our own
DSI flush against `esp_lcd`.

### Physical controls — not on this board

The CrowPanel Advance is a bare touchscreen: **there is no rotary encoder and there are no
transport buttons.** The design's Ø36 push-select dial and 4× Ø13 caps have to be added as
external hardware on the 11-pin GPIO header / Crowtail connectors. That is a hardware task with
its own BOM, and it is why the UI work starts touch-only.

Implication for `core/board.h`: when the controls do arrive, add a generic API rather than four
bespoke functions, mirroring how wake-word phrases are handled (the board reports *which* input
fired; the unit decides what it means):

```c
// --- Momentary buttons (optional; 0 on boards without any) ---
int  buttonCount();
int  buttonPoll();              // index of a press since the last call, else -1
const char *buttonName(int i);  // "play" | "next" | "prev" | "rooms" — for logs/UI
```

Boards without buttons return 0/-1/nullptr, so nest and sleep-machine stay untouched.

### UI sound feedback (two onboard speakers)

The board has an NS4168 amp driving two speakers. Use them for **UI feedback** — a click on
touch, a tick per volume step, a confirmation on room change — and make it **configurable**
(on/off + level, persisted in `core/settings`, exposed on the Settings screen).

Notes for whoever implements it:
- `PIN_AUDIO_CTRL` (GPIO30) gates amp power. Drive it **low when idle** — leaving a class-D amp
  enabled between clicks wastes current and hisses. Enable it a few ms before a sample and drop it
  after a short idle timeout.
- Feedback samples are tiny; keep them as PROGMEM PCM and push them straight to I2S. Do **not**
  pull in the Helix MP3 decoder for this — the sleep-machine needs that, the jukebox doesn't.
- This is UI feedback, not media playback. It should not touch `localAudio*` in the board HAL
  (that contract means "play a file off local storage"); a separate small `uiSound()` is cleaner.

## Sequence

1. **Screen — DONE, all three stages confirmed on hardware.**
   `[env:jukebox-bringup]` + `src/boards/crowpanel_p4_7in/display_test.cpp`.
   The EK79007 comes up at 1024x600, DSI 2 lanes @900 Mbps, DPI 52 MHz, and a hand-drawn
   design-system frame renders with correct colours (`rgb_ele_order = RGB`) and no artefacts.
   Headroom is excellent: **428 KB internal heap free**, and the 1200 KB frame buffer lands in
   PSRAM, leaving internal RAM alone.

   **Stage 3 (LVGL 9 + GT911) works.** LVGL renders in `LV_DISPLAY_RENDER_MODE_DIRECT` straight
   into the DSI frame buffer — no second buffer, no blit; the flush callback only writes the
   dirty rows back out of cache. Touch is a hand-written GT911 reader (~60 lines of I2C) rather
   than another managed component.

   Measured, with an animation running so the figures mean something:

   | | |
   |---|---|
   | renders/sec | **~32 idle, ~50 under touch** — LVGL's default 33 ms `LV_DEF_REFR_PERIOD`, not a compute limit |
   | dirty rows/sec | 3.5k–6k (~7–12 MB/s) |
   | internal heap free | **328 KB** after panel + LVGL |

   Two measurement traps worth avoiding: a *static* screen idles around 480 loops/sec because
   LVGL finds nothing dirty and never renders, and even with an animation the loop rate is
   bounded by `delay()` and the refresh period. Count **flush callbacks**, not loop iterations.

   **GT911 coordinate decode.** The widely-quoted register map puts a track-ID byte at 0x8150
   with x_lo at p[1]. This controller does not — coordinates start at p[0]. Decoding with the
   track-ID offset yields values like `0x5003`, an x-high byte sitting where the low byte was
   expected. Raw bytes `C1 03 46 02 ...` decode as (961, 582) for a bottom-right press on a
   1024x600 panel, which also confirms **no axis swap and no inversion** is needed: X runs
   left→right over 1024, Y top→bottom over 600. `GT911_RAW_DUMP` in `display_test.cpp` prints
   the raw block and both candidate decodings; it also *latches* the last touch and reprints it
   every second, because otherwise reading touch over serial is a synchronisation game where an
   empty capture window is indistinguishable from broken hardware.

   Three things cost real time here — see also `lib/esp_lcd_ek79007/VENDORING.md`:

   - **The frame buffer needs an explicit cache write-back.** It lives in PSRAM and the DSI DMA
     reads it directly, bypassing the CPU data cache, so after any CPU drawing you must
     `esp_cache_msync(fb, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M)`. Without it the panel shows only
     the cache lines that happened to be evicted: the image comes out shredded into vertical
     stripes of otherwise-correct colour over an unwritten background. It looks exactly like a
     DSI timing or lane fault. **Diagnostic shortcut:** `esp_lcd_dpi_panel_set_pattern()` draws
     its bars inside the DSI peripheral with no frame buffer involved, so "hardware bars perfect,
     our drawing shredded" identifies a missing cache sync rather than a broken panel.
   - **The MIPI D-PHY needs internal LDO channel 3 at 2500 mV** (`esp_ldo_acquire_channel`).
     Miss it and the DSI bus initialises without complaint and the panel just stays dark.
   - **The driver is vendored into `lib/`, not pulled as a managed component**, because
     pioarduino builds Arduino against prebuilt IDF libs and never compiles a newly added
     component into the link.
2. **Prove multicast over ESP-Hosted** (item 3 above) — **written and building**:
   `[env:jukebox-mcast]` + `src/boards/crowpanel_p4_7in/multicast_test.cpp`. It replays the exact
   request `core/sonos/ssdp.cpp` sends and reports PASS/FAIL per layer (association → DNS/TCP →
   M-SEARCH from :1900 → M-SEARCH from an ephemeral port → multicast group join → HTTP to a
   discovered speaker), then prints a verdict. The ephemeral-port case matters: `ssdp.cpp` does
   `udp.begin(1900)`, and if only the fixed source port is broken over the SDIO bridge, the fix is
   one line rather than an architecture change. **Not yet run on hardware.**
3. **Port `core/` to Arduino 3.x** — **compiles clean.** All 3,127 lines of `src/core/` build
   against Arduino 3.3.11 / IDF 5.5 with a single shim: Arduino 3.x renamed
   `MDNSResponder::IP(idx)` to `address(idx)`, so `net/registrar.cpp` has a version-conditional
   `mdnsResultIp()`. Nothing else — `WiFi`, `HTTPClient`, `Preferences`, `WiFiUDP`, `ArduinoOTA`
   and `WebServer` all compiled unmodified, which was better than expected. `nest` and
   `sleep-machine` were rebuilt afterwards and are green, so the shim is genuinely portable.

   `[env:jukebox-core]` compiles `core/` with no board or unit purely to catch regressions
   against 3.x; it is not meant to link.

   **Compiling is not running.** Nothing in `core/` has executed on this board yet. Two areas to
   distrust until they have:
   - `WebServer`. The sleep-machine deliberately bypasses its multipart and raw body paths
     because both are broken in 2.0.17 (see CLAUDE.md). Those workarounds may be unnecessary, or
     actively wrong, on 3.x. That code is board-side (es3c28p) so it does not affect the jukebox
     directly, but the same reasoning applies to anything new we serve here.
   - `HTTPClient` chunked reads, which album art depends on (`writeToStream()`).

4. **Runtime-prove `core/` on the P4** — **done, `core/` runs unmodified.** `[env:jukebox-smoke]`
   + `src/boards/crowpanel_p4_7in/core_smoke_test.cpp` drives the real shared code on hardware:

   ```
   [PASS] NVS round-trip     Preferences ok
   [PASS] wifiConnect()      ip=192.168.68.70  ssid=RB-West
   [PASS] ssdpDiscover()     9 zone(s) in 642 ms
   [PASS] GetTransportInfo   [PASS] GetVolume vol=38   [PASS] GetPositionInfo
   ```

   With music playing, **all 8 checks pass**, including the two risky ones:

   ```
   [PASS] DIDL parse           fields populated   (title/artist/album, artUri single-escaped)
   [PASS] album art (chunked)  222 KB, JPEG SOI ok   — 228,077 bytes in 1353 ms
   ```

   So `core/sonos/ssdp.cpp`, `soap_client.cpp`, `didl.cpp` and the `HTTPClient::writeToStream()`
   de-chunking all work on this silicon, **with `core/` byte-identical to what the S3 units run**.

   ### ⚠️ Latent bug found in shared code: album art >220 KB is silently truncated

   `core/album_art.cpp` has `JPEG_MAX = 220 * 1024` (225,280 B). The very first real cover tested
   here was **228,077 B** — over the buffer. `BufSink` stops appending at the cap and
   `albumArtFetch()` only rejects `got < 100`, so an oversized cover is truncated with no error
   and then fails (or garbles) in TJpg. **This affects nest and sleep-machine equally** — it is
   not P4-specific and predates this work. Fix is one constant plus a truncation check; PSRAM is
   nowhere near the constraint (~7 MB free on the S3, 31 MB here).

5. `src/units/sonos_jukebox/` — Now Playing → Rooms → Radio, translating the design tokens into
   an LVGL style header that mirrors the `--token` names.
6. UI sound feedback + settings toggle.
7. External dial + 4 buttons: hardware, then the `buttonCount/buttonPoll/buttonName` HAL.
8. OTA + portal registration (`core/net/`), already board-agnostic.

## ✅ SOLVED: the C6 wedged on a warm reset — Arduino used the wrong reset pin

**This shapes the whole development loop, so read it before debugging anything network-related.**

Wi-Fi works only on the first boot after a **full power cycle** (USB-C unplugged and replugged).
After any P4-only reset — flashing, an EN pulse, or a panic reboot — the next boot does this, and
every boot after it, until power is removed:

```
[   47][I][esp32-hal-hosted.c:290] hostedInit(): Initializing ESP-Hosted
E (13050) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x107   (ESP_ERR_TIMEOUT)
E (13050) H_SDIO_DRV: failed to read registers
rst:0xc (SW_CPU_RESET)   ... loops forever
```

**Root cause: Arduino ignores the ESP-Hosted pins from sdkconfig.** `esp32-hal-hosted.c` seeds
`sdio_pin_config` from the board VARIANT's `BOARD_SDIO_ESP_HOSTED_*` macros, then overwrites the
Kconfig values before calling `esp_hosted_sdio_set_config()`:

```c
conf.pin_reset.pin = sdio_pin_config.pin_reset;   // clobbers CONFIG_..._RESET_SLAVE=32
```

Our board JSON uses `"variant": "esp32p4"`, i.e. Espressif's Function EV Board, whose macros are
CLK/CMD/D0/D1 = **18/19/14/15 — identical to the CrowPanel, which is why Wi-Fi worked at all** —
but `BOARD_SDIO_ESP_HOSTED_RESET 54`, not 32. So the C6 was never reset. A warm P4 reset left it
running a stale session: the card re-enumerates, the first register read times out, and
`H_TRANSPORT_RESTART_ON_FAILURE` makes esp_hosted reboot the host on purpose — forever, since the
reboot cannot reset the C6.

GPIO32 is confirmed as net **C6_EN** (IC1.EN, 10k pull-up, active high) in Elecrow's own Eagle
schematic, and their ESPHome example uses `reset_pin: GPIO32, active_high: true`. Not a hardware
limitation.

**Fix:** call `WiFi.setPins(18, 19, 14, 15, 16, 17, 32)` before *any* Wi-Fi call (it is refused
once ESP-Hosted has initialised; all seven pins must be >= 0). **Verified on hardware: a warm
reset now recovers and Wi-Fi comes straight back up.**

**The durable fix is now in place:** `variants/crowpanel_p4_7in/pins_arduino.h` (a copy of the
stock `esp32p4` variant with that one line changed) plus `board_build.variants_dir = variants`.
No app code calls `setPins` any more. Verified with `setPins` removed: two consecutive host
resets, Wi-Fi up both times, no SDIO errors.

Caveat worth knowing: setting `variants_dir` makes PlatformIO look for variants **only** in the
project folder, so any variant named by a board json here must exist under `variants/`.

Two traps for later:
- **Data lines differ by PCB revision.** 7" V1.0: d0=14,d1=15,d2=16,d3=17. **V1.1/V1.2 reverse
  them** (d0=17,d1=16,d2=15,d3=14). Reset stays 32 on all. Ours is V1.0; the revision is printed
  on the top silkscreen.
- **GPIO54 is the swappable radio header's NRST** per Elecrow's wiki — ESP-Hosted was pulsing it
  every boot. Harmless with the C6 fitted, but not a no-op if a LoRa module is ever installed.

Consequences, all of which cost time here:
- **Every `-t upload` must be followed by a power cycle** before Wi-Fi will work.
- **Do not reset the board to "see the output properly".** That re-wedges the C6 and destroys the
  run you were trying to observe. Have the firmware reprint its result on a timer instead
  (`core_smoke_test.cpp` reprints the whole table every 5 s) and read at leisure.
- **Any experiment run after a failure is measuring a wedged C6, not your change.** Four separate
  hypotheses were "falsified" here against a board that could not have passed regardless. Always
  re-establish a known-good control on a freshly power-cycled board first.

Optional hardening, not the fix: `CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n` turns the
unrecoverable boot loop into an `ESP_HOSTED_EVENT_TRANSPORT_FAILURE` event the app can handle.
Worth considering for a wall-mounted unit as a safety net.

## Case notes

`hardware/jukebox-7/`, generated with the existing Python CSG toolchain (trimesh + manifold3d)
off `tokens/hardware.css`. Hardware commits stay separate from firmware.

The design's screen cutout is essentially already right — it specifies 154×86 mm and the real
active area is **155×87 mm**. The face, however, needs rework: the design assumes a 210 mm face
with a 46 mm control column beside the screen, but the **PCB alone is 180 × 105 mm**, so the
column cannot overlap it. Either widen the face to roughly 180 + column, or mount the dial and
buttons on a separate sub-panel. Re-derive from the real PCB outline (Elecrow ships a `.stp`
model and Eagle files in their GitHub repo) before printing anything.

## Open questions

- Does the dial's RGB light-ring ship in v1? It would be the first software-controllable LED in
  the project (`PIN_LED` GPIO48 exists on this board, but the ring is external).
- Camera header is present and unused. Out of scope unless you want presence-wake.
- Accent locked to amber unless you say otherwise.
