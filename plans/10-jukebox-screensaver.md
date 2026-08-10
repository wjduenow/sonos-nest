# 10 — sonos-jukebox screensaver: options, costs, and what the hardware will actually do

Status: **Tier 0, A and B are BUILT** (see §7). D and E are still research. Written to answer one
question — what should the 7" wall panel show when nobody is touching it, and what does each
answer cost?

Two motivations, and they pull in different directions:

- **Burn-in.** Real, but smaller than it sounds — see "What burn-in actually means here".
- **Aesthetics.** A 1024x600 panel on a wall showing a static Now Playing screen 24/7 is a waste
  of the nicest hardware in the project.

---

## 1. What we are working with

Everything below is **measured**, not spec-sheet, unless marked. Live device
(`GET http://192.168.68.85/api/config` → `.health`, uptime 1 h 36 m, playing):

| Resource | Total | Free now | Worst ever | Notes |
|---|---|---|---|---|
| **Internal SRAM** | — | 90 KB | **67 KB min, largest block 31.7 KB** | The binding constraint. Always has been. |
| **PSRAM** | 32 MB | **30.69 MB** | — | Effectively unlimited for this purpose. |
| **LVGL pool** | 512 KB | 459 KB | peak 52.7 KB used, 1% frag | Lots of room. |
| **App slot** | 6.25 MB | **4.16 MB** (2.09 MB used) | — | Dual-OTA, so this is per-slot. |
| **`spiffs` partition** | 3.375 MB | all of it | — | Declared, never mounted. Free real estate. |

Silicon and buses:

- **ESP32-P4**, dual RISC-V HP @ **360 MHz**, L2 cache 256 KB, **PSRAM in hex mode @ 200 MHz**
  (fast — this matters for full-screen blits).
- **Hardware JPEG codec.** `esp_driver_jpeg` and `esp_driver_ppa` (Pixel Processing Accelerator:
  scale / rotate / blend / colour-convert) are both present as headers *and* prebuilt `.a` files in
  the pioarduino P4 libs, and `CONFIG_SOC_JPEG_DECODE_SUPPORTED=y` in our generated sdkconfig. Not
  yet proven to link from this Arduino build — see the spikes in §5.
- **No hardware H.264 *decoder*.** The P4's H.264 block is **encode-only** (1080p30). Decode is
  software (`esp_h264`/openh264/tinyh264) and Espressif's own figures are ~11 fps at 640x480 with a
  dual-task decoder. **This single fact decides the whole video question**: MJPEG yes, H.264 no.
- **Display**: 1024x600 RGB565, **one** frame buffer (1.17 MB, PSRAM), LVGL renders in
  `LV_DISPLAY_RENDER_MODE_DIRECT` straight into the buffer the DSI is scanning out. There is no
  back buffer today, so any full-screen animation would tear. `esp_lcd_dpi_panel_config_t` has
  `num_fbs` — going to 2 costs another 1.17 MB of PSRAM, which we have.
- **microSD**: SDMMC **slot 0**, **1-bit @ 10 MHz**. D1–D3 are not wired on this board, so 4-bit is
  not available. Ceiling is ~1.2 MB/s theoretical, realistically well under 1 MB/s.
  Elecrow chose 10 MHz "to improve stability"; **20–25 MHz is worth a measurement**, it would
  roughly double every video number below.
- **Wi-Fi**: ESP-Hosted to the C6 over SDIO **1-bit @ 10 MHz** (slot 1 — a *different* controller
  from the card, so SD traffic and radio traffic do not contend). Espressif's best published figure
  is ~36 Mbps at 4-bit/40 MHz; ours is 1/16 of that clock-width product, so expect **single-digit
  Mbps**. Unmeasured. And CLAUDE.md already records this link **dying under sustained load**
  (`rssi=0` while `wifi=3`, recovered only by reboot). Treat any plan that needs sustained network
  throughput as high-risk until measured.
- **Backlight**: LEDC PWM on GPIO31, `backlightSet(0..100)` already exists in the board HAL.
- **Touch stays alive with the backlight off** — GT911 is an independent I2C device, so
  touch-to-wake is free.
- **A PDM microphone is on the board and completely unused** (`PIN_MIC_CLK 24`, `PIN_MIC_SDIN 26`).
- **No ambient light sensor**, but the Crowtail I2C and UART connectors are free.

---

## 2. What burn-in actually means here

This is an **IPS LCD**, not OLED. IPS panels get *image retention* — a temporary ghost from a
static high-contrast image held at high brightness, which fades on its own — not the permanent
emitter wear that makes OLED burn-in a real hardware failure. Industry guidance for static-UI
industrial panels is consistent: retention is reversible; the mitigations that matter are
**brightness control** and **not holding one static high-contrast layout for hours**.

Practical consequence: **a screensaver is 80% an aesthetics project and 20% a burn-in project**,
and the 20% is almost entirely solved by the cheapest item on the list — dim/turn off the backlight
when nobody is there. That is worth saying out loud before spending two weeks on a video decoder.

It also means the panel's biggest static-contrast offenders are worth a second look regardless of
what the screensaver does: the always-on nav rail (five fixed glyphs, high contrast, never moves)
and the amber accent dot. A 1–2 px content drift every few minutes costs nothing and removes them
as a risk permanently.

---

## 3. The options

Complexity is calendar-ish for one person who knows this codebase. Resource figures are *deltas*
on top of the baseline in §1.

### Tier 0 — Idle state machine + backlight ramp *(prerequisite for everything else)*

Idle timer in `uiTick()`; wake on touch, dial turn, dial press, or a GENA track-change event.
Two-stage: dim to N% after a short idle, off (or screensaver) after a long one. Both intervals
configurable in the web admin and on-screen Settings, alongside the existing brightness setting.

- **SRAM** ~0 · **PSRAM** 0 · **Flash** <2 KB · **LVGL pool** 0
- **Complexity: S** (half a day)
- Solves the burn-in half of the brief on its own, and cuts panel power for most of the day.

### A — Clock + now-playing ambient card *(the nest's screensaver, grown up)*

Large clock, date, room, a quiet now-playing line, on near-black. The whole group drifts slowly
around the screen (a slow Lissajous over ~20 minutes reads as "alive", not "broken") which doubles
as pixel-shifting.

- **SRAM** ~0 · **PSRAM** 0 · **Flash** +25–60 KB (one large numeral-subset font)
- **LVGL pool**: 0 if we ship a real big font; **+60–120 KB transient if we scale a small one** —
  `transform_scale` allocates full ARGB draw layers from the pool, which is exactly what nearly
  froze the nest. Ship the font.
- **Complexity: S–M** (1 day)
- Lowest risk, and by itself a perfectly good answer.

### B — Album-art wallpaper *(best beauty per byte)*

Full-bleed, blurred, upscaled current cover as the background; sharp cover + title + clock over it.
Content changes with every track, so it is self-shuffling. PPA does the scale and the blend in
hardware. Art is already fetched and decoded — but `ART_MAX_PX` caps the decode at 280 px, so the
screensaver path needs a higher-resolution decode of the same URL.

- **SRAM** +2–4 KB (PPA driver) · **PSRAM** +1.2 MB (one 1024x600 RGB565 scratch), +2.4 MB if we
  crossfade between tracks · a 600 px cover at RGB565 is 720 KB on top of the existing 526 KB buffer
- **Flash** +10–20 KB
- **Complexity: M** (2–3 days), most of it in the second decode path and PPA bring-up
- Needs `num_fbs=2` if the drift is to be smooth. That is a change to `display.cpp`, which is
  delicate code — see the spike list.

### C — Photo slideshow from SD, uploaded from the web admin

New upload endpoint on the existing port-80 `WebServer`, a picker in the admin page, JPEGs decoded
by the hardware codec, PPA letterboxes them to 1024x600, slow crossfade.

- **SRAM** +8–12 KB (decode task stack + driver) · **PSRAM** +2.4 MB (two full-screen buffers)
- **Flash** +15–25 KB · **SD**: ~150–300 KB per 1024x600 JPEG, so 100 photos ≈ 25 MB
- **Complexity: M–L** (4–6 days) — the *upload path is the bulk of it*, not the display.
  CLAUDE.md documents exactly why, from the sleep-machine: on this Arduino, `WebServer`'s multipart
  parser reads the body **one byte at a time**, and its raw path never parses arguments *and* blocks
  waiting for a full buffer. The sleep-machine solved it with a second bare `WiFiServer` socket and
  its own read loop; copy that, don't re-derive it. Also copy its **409-while-busy** rule: the radio
  crawler writes this same card, and writing underneath it is how you corrupt a cache.
- Upload speed will be SD-write-bound (~180 KB/s measured on the sleep-machine's card), so a 250 KB
  photo is ~1.5 s. Fine.

### D — MJPEG video loop from SD

The hardware JPEG decoder makes the decode side genuinely easy (the P4 does 720p@88fps class work,
and someone has publicly played **1024x592 @ 30 fps MJPEG AVI** on a P4 board). **The constraint is
not the decoder, it is our SD card bus.**

At 1-bit/10 MHz (~1.2 MB/s ceiling, realistically ~0.8):

| encode | bytes/frame | fps we can feed |
|---|---|---|
| 1024x600 q70 | 60–90 KB | **~10–13 fps** |
| 800x450 q70 (PPA upscales) | 40–55 KB | ~15–20 fps |
| **640x360 q75 (PPA upscales)** | 25–35 KB | **~25–30 fps** |

So: **encode small and let the PPA upscale.** A 60-second 640x360@30 loop is ~54 MB on the card.

- **SRAM** +12–20 KB · **PSRAM** +1.17 MB (second frame buffer) +0.5–1 MB (frame ring) + C's buffers
- **Complexity: L** (1–2 weeks)
- Extra risks: continuous decode + full backlight in a sealed wall case is a thermal question nobody
  has asked yet; and this is the first thing on the device that would run the CPU hard indefinitely.

### E — Streaming video over the network (the "YouTube" question)

**Direct YouTube on the device: no.** Three independent blockers, any one of which is fatal —
no hardware H.264 decode (software gets ~11 fps at 640x480); YouTube requires TLS + adaptive DASH +
signature deciphering that changes on Google's schedule and would need a JS interpreter; and our
Wi-Fi link is a single-digit-Mbps SDIO bridge that already falls over under load. Don't spend time
here.

**The route that does work: let the portal Pi transcode.** `sonos-portal` already runs in Docker on
192.168.68.99 and is already the device's OTA source, so the trust path exists. Add one endpoint:

```
GET /api/screensaver/stream?src=<youtube url | file | playlist>
    → multipart/x-mixed-replace MJPEG, 800x450, 15 fps, ~2 Mbps
```

`yt-dlp | ffmpeg` on the Pi, where those problems are solved. The device just reads a multipart
stream and hardware-decodes each part — the *same* decode path as D, with the SD reader swapped for
an HTTP reader. This is the honest answer to "can it stream from YouTube": **yes, via the Pi, never
directly.**

- **Device**: same as D minus the SD reader · **Pi**: ffmpeg + yt-dlp, an evening's work
- **Complexity: M** on the Pi + **M** on the device, *after* D exists
- **The risk is the link, not the code.** ~2 Mbps sustained on a transport with a known
  under-load failure mode. Any implementation needs a hard rule: stall → fall back to the clock,
  never retry into the fault. And **never** "recover" by re-initialising the transport —
  `esp_hosted_deinit()` under live lwIP users hard-freezes the device.

### F — Net-new ideas worth putting on the table

1. **Mic-reactive visualiser.** There is an unused PDM microphone on this board. A wall panel next
   to the speakers it controls, drawing a real spectrum of what is actually playing in the room, is
   a *much* better idea than a fake one — and it is on-brand in a way a photo frame is not.
   Cost: I2S PDM RX + a small FFT. **SRAM +9–15 KB** — this is the one option that spends the scarce
   resource, and with a 31.7 KB largest free block it needs care (the sleep-machine's I2S buffers
   cost 33.8 KB at `8/1024` and 9 KB at `4/512` with no detection loss; same lesson applies).
   **Complexity: M.** Privacy note: local-only, nothing recorded, nothing transmitted — but it is a
   microphone on a wall and that deserves a switch in Settings.
2. **Album-art mosaic.** A slowly drifting grid of covers already on the card — `artcache`,
   `favcache` and the 1055-station `radioart` tree. Zero new assets, zero uploads, and the content
   is personal by construction. **Complexity: S–M**, and it is mostly B's machinery.
3. **House dashboard.** Weather / calendar / whatever the Pi already knows, rendered in LVGL. Near
   zero device resources, and the most *useful* thing a wall panel can show when idle.
   **Complexity: S** on the device, M on the Pi.
4. **Network photo frame** (Immich / Nextcloud / a folder on the Pi). Option C without the upload
   endpoint — the Pi pre-resizes to 1024x600 so the device only decodes. Arguably strictly better
   than C: it removes the single hardest part (upload) and the SD write contention. **Complexity: M.**
5. **Generative ambient.** Slow gradient field / drifting particles via PPA blends. No storage, no
   network, guaranteed motion. **Complexity: S–M.**
6. **Presence wake.** An LD2410C mmWave module on the free Crowtail UART (~$5) turns the panel on as
   you walk up and off when you leave. This is the best *experience* answer to burn-in and power,
   and it makes an aggressive idle timeout painless. **Complexity: S** once the part is in hand.
7. **Ambient light sensor** (BH1750 on the free I2C bus, ~$2) for auto-brightness. A wall panel at
   100% at 2 a.m. is the actual complaint most of the time. **Complexity: S.**

---

## 4. Recommendation

Sequence, so each step ships something and nothing is blocked on a decision made too early:

1. **Tier 0** — idle + backlight ramp + touch/dial/event wake. Half a day. Solves burn-in.
2. **A** — clock/now-playing ambient with drift. One day. Now it is also nice to look at.
3. **B** — album-art wallpaper. Two to three days. This is where it starts to feel like a product,
   and it needs no new storage, no uploads and no new network traffic.
4. Then choose by taste, not by capability, between **F2 (mosaic)**, **F4 (network photo frame)** and
   **C (SD upload)**. F4 is the same result as C for less than half the work.
5. Treat **D/E (video)** as a separate project, gated on the spikes below. It is the only item here
   that can destabilise a device that currently works.

Worth doing alongside, independent of which option wins: **F7 (light sensor)** and **F6 (presence)**
are each a few dollars and a few hours, and they improve every option on the list.

---

## 5. Spikes to run before committing to anything past step 3

Each is half a day, and each retires a real unknown rather than confirming a guess.

1. **Instrument the unknowns.** Expose `sdCardFreeBytes()` (it exists, nothing surfaces it), a
   one-shot SD sequential-read rate, and a Wi-Fi throughput self-test in `/api/config` → `.health`.
   Every number in §3's video tables is currently arithmetic, not measurement.
2. **Prove the hardware JPEG path.** One file, decode with `jpeg_decoder_process()`, report ms/frame
   at 1024x600, confirm `esp_driver_jpeg` links under *this* pioarduino build. If it doesn't link,
   C/D/E all change shape.
3. **`num_fbs = 2`.** Confirm the DPI panel and LVGL `RENDER_MODE_DIRECT` still behave with two
   buffers, and that `flushCb`'s `esp_cache_msync` still targets the right one. Required for any
   smooth full-screen motion; `display.cpp` is delicate and this should fail early if it fails.
4. **SD at 20–25 MHz.** Elecrow picked 10 MHz for stability, not from measurement. Doubling it
   doubles every fps figure in D.

## 6. Standing traps that apply to this work

- **Keep it out of `core/`.** `sonos-button` sweeps every core file into its build with `+<core/>`
  and no LVGL, no TJpg in `lib_deps`. A screensaver file in `core/` breaks the one env nobody builds
  by habit — that is exactly how `art_cache.cpp` broke it for weeks. Put this in
  `units/sonos_jukebox/` and `boards/crowpanel_p4_7in/`, or add the exclusion.
- **Every new buffer goes in PSRAM** (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`). Internal
  `heapLargest` is 31.7 KB; on the nest it was *fragmentation*, not exhaustion, that starved LWIP and
  surfaced as Sonos "connection refused".
- **Tag allocations with `heapwatch::note()`** so `health.heapLow` attributes any regression. It has
  already overturned one confident guess about who was eating the heap.
- **The board must not reach into `g_pending` or `settings`.** Whatever the idle timer wakes, it goes
  through the unit, the same way the wake-word phrases do on the sleep-machine.
- **Power-cycle after every upload**, and read `[health]` before diagnosing any "hang".

---

## 7. What shipped (Tier 0 + A + B)

All of it is in `units/sonos_jukebox/screens.cpp` (the screensaver block near the bottom) plus four
settings in `core/settings.*` and `core/webconfig.*`. Cost: **+41 KB of flash**, of which 40 KB is
the clock font. No PSRAM, no new task, no measurable internal SRAM.

- **State machine**: Awake / Showing / Blank, off `lv_display_get_inactive_time()`.
- **Wake**: any touch, or any turn/press of the dial — `handleDial()` calls
  `lv_display_trigger_activity()`, because the dial is polled over I2C and is not an LVGL input
  device, so LVGL would otherwise never see it.
- **Clock (A)**: 120 px time + meridiem + written-out date. A REAL font
  (`lv_font_clock_120.c`, digits/colon/hyphen only), not a scaled label — read that file's header
  before "simplifying" it.
- **Wallpaper (B)**: the current cover scaled to fill under a 70 % scrim, sharp tile + clock +
  track over it. `AUTO` mode falls back to the clock when nothing is playing.
- **Drift**: one container steps along a slow Lissajous, on the minute so the move and the clock
  repaint are one event. Nothing else on screen moves, so a step invalidates one rectangle.
- **Settings** (web page and on-screen Settings, same `core/webconfig` fields): `saver_mode`,
  `saver_delay_sec`, `saver_dim`, `saver_blank_min`. Both timers are presets in both places.
- **Bonus fix**: `settingsBrightness()` is now actually applied on this unit. It had been in NVS
  and on the web page all along; `uiInit()` set 100 % and nothing ever read it again.

Two things a future change should not undo:

- The overlay stays up in the **Blank** state even with the screensaver set to Off. That is what
  eats the wake-up tap so it does not press an invisible button.
- `saverApplyLayout()` forces a clock repaint. The meridiem is hung off the time's right edge with
  `lv_obj_align_to()`, which resolves once — without the repaint it is left behind when the layout
  changes.

## Sources

- [ESP32-P4 JPEG Encoder and Decoder — ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/jpeg.html)
- [Playing 1024x592 30fps MJPEG AVI video on an ESP32-P4 — Adafruit](https://blog.adafruit.com/2025/02/12/playing-1024x592-30fps-mjpeg-avi-video-on-an-esp32-p4/)
- [Simple-LVGL-Player (ESP32-P4, MJPEG from SD, ek79007 1024x600)](https://github.com/espzav/Simple-LVGL-Player)
- [ESP-H264 practical usage guide — Espressif Developer Portal](https://developer.espressif.com/blog/2025/07/esp-h264-use-tips/) (decode fps figures)
- [esp-hosted-mcu SDIO transport docs](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/sdio.md)
- [IPS panel burn-in vs image retention — CDTech](https://www.cdtech-display.com/knowledges/ips-panel-burn-in-real-risks-image-retention-prevention-and-lifespan/)
- [Ghosting & burn-in on IPS TFT panels — Focus LCDs](https://focuslcds.com/journals/ghosting-burn-in-on-ips-tft-panels/)
