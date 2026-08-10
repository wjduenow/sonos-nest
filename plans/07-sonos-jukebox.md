# 07 — sonos-jukebox (third form factor)

**Board:** ELECROW **CrowPanel Advance 7" ESP32-P4 HMI AI Display** (DHE04107D, PCB **V1.0**) —
1024×600 IPS MIPI-DSI, GT911 touch, dual speakers, ESP32-C6 for Wi-Fi.

**Status: MERGED TO MAIN and in daily use.** Panel, touch, LVGL 9, Wi-Fi, zone discovery and
switching, transport, album art, OTA, portal registration, UI sound, the rotary dial, and five
screens (Now Playing · Favourites · Radio · Rooms · Settings) all run on hardware. `src/core/` is
shared with the S3 units, with one Arduino-3.x shim.

Since the first pass this unit also gained: **Rooms grouping** (checkbox per room, UNGROUP,
per-room volume and play/pause, live per-room status), **UPnP GENA eventing** for Now Playing
(plans/09 — Sonos pushes state, the poll drops to a 15 s backstop), a **TCP log mirror** on
:2323, a **favourites refresh schedule** in both the web admin and on-device Settings, and a
substantial round of **heap work** (low-water 1.4 KB → 50+ KB).

**Not done:** the four transport buttons (a PCF8574 on the shared I2C bus — the dial works, the
caps were dropped from the case design), the case itself, the **untested Wi-Fi provisioning path**
(see *Open items*), and one **unresolved network fault** (the ESP-Hosted link dies under load;
recovered by an automatic reboot, not cured — though the mbedTLS/PSRAM fix removed the load
profile that was triggering it nightly).

A wall-mounted Sonos controller: a large landscape touchscreen with a physical control column to
its right — a push-to-select rotary dial over a 2×2 grid of momentary caps (play/pause · skip
forward · skip back · change rooms). Matte-white printed case, flush wall plate, rear USB-C.

---

## Resuming this work — read this first

**Everything is on `main`.** The `feat/sonos-jukebox` and `feat/gena-eventing` branches are both
merged; there is no side branch to hunt for.

**START BY READING THE DEVICE, NOT THE CODE.** `GET /api/config` → `.health` now answers most
questions in one curl, and every field there exists because something was once diagnosed blind:

    heapFree/heapMin/heapLargest   fragmentation kills this board before exhaustion does
    heapLow {tag,free,largest}     WHICH subsystem owned the low-water (core/heap_watch.h)
    gena {subscribed,events,...}   is eventing carrying the screen, or is the poll?
    nowPlaying {...,artFetch,artFail,artClear}
                                   what the device BELIEVES is playing, and whether the art
                                   pipeline is actually succeeding
    lvMemUsed/lvMemMax             the LVGL pool that has frozen this UI before
    soapCalls/soapReconnects       traffic and socket churn

Plus `nc <ip> 2323` for the live log. A wall-mounted unit has no cable; this is the way in.

Three things will waste your time if you don't know them, in order of how much they cost here:

1. **Power-cycle after every upload.** A P4-only reset used to wedge the C6 forever; that is
   fixed (see *SOLVED: the C6 wedged on a warm reset*), but the habit is still correct because
   the C6 needs its reset to actually fire, and a wedged co-processor makes every subsequent
   experiment measure the wrong thing. **Any experiment run after a failure may be measuring a
   broken radio rather than your change.** Four hypotheses were "falsified" here against a board
   that could not have passed regardless. Re-establish a known-good control on a freshly
   power-cycled board before believing any negative result.
2. **Never open a serial capture window while asking the user to act.** An empty window is
   indistinguishable from broken hardware. Have the firmware **latch** state and reprint it on a
   timer, then read at leisure. This bit twice.
3. **Read the health heartbeat before diagnosing any "hang".** It is the single most useful
   diagnostic on this device — see the playbook below.

### The health heartbeat, and how to read it

`uiTick()` prints this every 10 s:

```
[health] up=81s heap=70KB min=49KB psram=31025KB wifi=3 rssi=-54 ip=192.168.68.59 zones=9 lvgl_free=67KB
```

Every link field (`wifi`, `rssi`, `ip`, `zones`) is a **snapshot published by netTask** (`g_link*`
in `core/app.h`), not a live call. That is deliberate and not cosmetic: `WiFi.RSSI()` and
`WiFi.localIP()` are blocking RPCs to the C6 on this board, and `sonos::zones()` is a vector
netTask rewrites — so the original version of this heartbeat *stalled or crashed the UI task in
exactly the fault it exists to report*. If you add a field here, publish it from netTask too.

Because it is printed **from the UI task**, its presence or absence separates two faults that look
identical on the glass — a frozen screen that ignores touch:

| Symptom | Heartbeat | Meaning |
|---|---|---|
| Screen stuck, no rail response | **absent** | The **UI task** is stuck. netTask is probably still fine (it only logs on events). Suspect the LVGL pool. |
| Screen responsive but a list is empty | **present**, `zones=0`, `rssi=0` | The **link** is dead. The UI is fine and has nothing to draw. |

`rssi=0` while `wifi=3` is the dead-link signature and is worth memorising: RSSI can only come
from the C6 over an RPC, so zero-while-connected means the RPC is dead and only the host's cached
association state remains.

---

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

The tokens are mirrored into `src/units/sonos_jukebox/ui_scale.h` under matching names, so the
LVGL styles can be diffed against the design by eye.

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

An **unidentified I2C device answers at 0x2F** — not in Elecrow's documentation, harmless so far.

### What this buys us

Much better fit for the design than an ESP32-S3 would have been:

- **768 KB internal L2MEM** vs the S3's ~150 KB free. Internal SRAM is *the* recurring failure mode
  in this repo — it is why nest OTA is unreliable and why the wake word starved LWIP into
  "connection refused". **That pressure is genuinely gone**: this unit idles at ~70–100 KB free
  internal heap with the whole app running, and PSRAM sits at ~31 MB free.
- **MIPI-DSI with a real display controller.** 1024×600 would have been marginal-to-impossible on
  an S3; here it is the intended use case, and the 1200 KB frame buffer lives in PSRAM.
- **Hardware JPEG codec + 2D-DMA + PPA** — not yet used. Album art still decodes in software via
  TJpg_Decoder. An easy future win.

### What it costs us — read before writing any code

**1. Different silicon ⇒ different toolchain.** The P4 is RISC-V and the official
`platform = espressif32` does not support it at *any* version. The jukebox envs use the
**pioarduino** fork (`55.03.311` = Arduino core 3.3.11 / IDF 5.5.5).

> ⚠️ The fork publishes itself under the name **`espressif32`** too. Installing it immediately
> retargeted `nest` and `sleep-machine` to Arduino 3.x — verified, then fixed by pinning
> `platform = espressif32@6.9.0` in `[env]` (6.9.0 is the last platform on
> framework-arduinoespressif32 ~3.20017, which the S3 units are written against).
> **Both pins are load-bearing. Don't loosen either.**

**2. Switching envs re-downloads the framework.** The P4 framework (3.3.11) and the S3 framework
(3.20017) install to the *same* `framework-arduinoespressif32` package directory, so alternating
`pio run -e nest` and a jukebox env re-fetches ~78 MB each way. Annoying, not dangerous.

**3. `custom_sdkconfig` makes PlatformIO build this as an IDF project.** It prints
"the 'src_filter' option cannot be used with ESP-IDF" — **cosmetic here**; `build_src_filter` was
verified to still select our sources correctly. It also means `ARDUINO_USB_CDC_ON_BOOT=1` breaks
the compile outright (`HardwareSerial.h: 'USBSerial' was not declared`); serial on this board is
the CH340K UART bridge anyway, not native USB CDC.

**4. Two build-system landmines, both already worked around.**

- *SCons.* The IDF 5.5 include list pushes compile commands past SCons' default `MAXLINELENGTH`,
  into a response-file path that is broken in the bundled SCons 4.8.1
  (`AttributeError: 'CmdStringHolder' object has no attribute 'data'`). It surfaces as a
  per-object-file failure, so it reads like a code error. Fixed by `tools/p4_maxlinelength.py`.
- *LVGL.* Vendored drivers must go in `lib/`, **not** as managed components — pioarduino builds
  Arduino against prebuilt IDF libs and never compiles a newly added component into the link.
  See `lib/esp_lcd_ek79007/VENDORING.md`.

**5. Elecrow's examples are LVGL 8.3.11**; this repo is on LVGL 9. Their `lvgl_v8_port.cpp` is not
reusable as-is; we write our own DSI flush against `esp_lcd`.

---

## Build / flash

```bash
export PATH="$PATH:$HOME/.platformio/penv/bin"
pio run -e sonos-jukebox -j 1 -t upload --upload-port /dev/ttyUSB0   # then POWER CYCLE
python3 tools/readser.py /dev/ttyUSB0 60                             # read serial
```

Build with `-j 1` or `-j 2` — this machine has a known hardware fault under load (random
SIGKILL/ICE; BIOS update pending). A failed build is often just that; **retry before debugging**.
The upload loop in this plan's history retries up to 3× for exactly this reason.

| env | what it is |
|---|---|
| **`sonos-jukebox`** | **the app** — board + unit + core |
| `jukebox-bringup` | standalone panel/LVGL/GT911 probe (`display_test.cpp`) |
| `jukebox-mcast` | SSDP-over-ESP-Hosted probe (`multicast_test.cpp`) |
| `jukebox-core` | compiles `core/` alone against Arduino 3.x to catch regressions; not meant to link |
| `jukebox-smoke` | runs real `core/` on hardware (`core_smoke_test.cpp`) |

`app.cpp`, `board.h`, `settings.*` and `album_art.cpp` are **shared core** — after touching any of
them, rebuild `nest` and `sleep-machine`. Both were verified green after this session's changes.

---

## What is built and working

### Board HAL — `src/boards/crowpanel_p4_7in/`

`display.{h,cpp}` · `touch.{h,cpp}` · `board.cpp` · `ui_sound.{h,cpp}` · `net_link.cpp` · `pins.h`,
plus the three standalone probes. Implements `core/board.h`. Stubs return neutral values for
everything this board lacks (encoder, knob, local audio, wake word, local files) so the shared
core needs no conditionals.

**Panel (stage 2).** EK79007 at 1024×600, DSI 2 lanes @ 900 Mbps, DPI 52 MHz,
`rgb_ele_order = RGB`. Frame buffer in PSRAM, ~428 KB internal heap free at this stage.

Three things cost real time — see also `lib/esp_lcd_ek79007/VENDORING.md`:

- **The frame buffer needs an explicit cache write-back.** It lives in PSRAM and the DSI DMA reads
  it directly, bypassing the CPU data cache, so after any CPU drawing you must
  `esp_cache_msync(fb, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M)`. Without it the panel shows only the
  cache lines that happened to be evicted: the image comes out shredded into vertical stripes of
  otherwise-correct colour. It looks exactly like a DSI timing or lane fault.
  **Diagnostic shortcut:** `esp_lcd_dpi_panel_set_pattern()` draws its bars inside the DSI
  peripheral with no frame buffer involved, so "hardware bars perfect, our drawing shredded"
  identifies a missing cache sync rather than a broken panel.
- **The MIPI D-PHY needs internal LDO channel 3 at 2500 mV** (`esp_ldo_acquire_channel`). Miss it
  and the DSI bus initialises without complaint and the panel just stays dark.
- The driver is **vendored into `lib/`** (see landmine 4 above).

**LVGL 9 + touch (stage 3).** `LV_DISPLAY_RENDER_MODE_DIRECT` straight into the DSI frame buffer —
no second buffer, no blit; the flush callback only writes the dirty rows back out of cache. Touch
is a hand-written GT911 reader (~60 lines of I2C).

Measured with an animation running: **~32 renders/sec idle, ~50 under touch** (LVGL's 33 ms
`LV_DEF_REFR_PERIOD`, not a compute limit), 3.5k–6k dirty rows/sec, **328 KB internal heap free**.
Two measurement traps: a *static* screen idles ~480 loops/sec because LVGL finds nothing dirty and
never renders, and the loop rate is bounded by `delay()` anyway. **Count flush callbacks, not loop
iterations.**

**GT911 coordinate decode.** The widely-quoted register map puts a track-ID byte at 0x8150 with
x_lo at p[1]. **This controller does not — coordinates start at p[0].** Decoding with the track-ID
offset yields values like `0x5003`, an x-high byte sitting where the low byte was expected. Raw
bytes `C1 03 46 02` decode as (961, 582) for a bottom-right press, which also confirms **no axis
swap and no inversion**: X runs left→right over 1024, Y top→bottom over 600.

### Unit — `src/units/sonos_jukebox/`

`screens.cpp` + `ui_scale.h`. Four pages behind a left nav rail: **Now Playing · Radio · Rooms ·
Settings**. Rail targets were enlarged 1.5× after the first hardware test — the design's sizes are
too small to hit reliably on glass.

Two LVGL behaviours that caused visible bugs here, both worth remembering:

- **LVGL paints in creation order.** Page containers created *after* the status bar covered it.
  There is no z-index to fall back on; create in the order you want painted.
- **Refresh gating must key on something that actually changes.** The room chips were gated on
  `g_zonesGen`, which does not move on a zone *switch*, so switching looked broken. Separately,
  calling `lv_label_set_text()` unconditionally every frame caused visible **title flicker** —
  gate per-field on an actual change.

### UI sound feedback — `ui_sound.cpp`

NS4168 amp → two onboard speakers over I2S. Three cues (`Tick` / `Confirm` / `Error`), volume
persisted in `core/settings` (`settingsUiSound()`, default 40, 0 = off) and exposed on Settings.

- **They are clicks, not beeps.** A tone says "a computer noticed"; a click says "a control
  moved", which is what the physical caps will eventually give you. Synthesised as a noise burst
  with a fast exponential decay and a one-pole lowpass — a pitched sine always reads as a beep
  however short it is. A deterministic LCG (not `random()`) makes every press sound identical.
- **`PIN_AUDIO_CTRL` (GPIO30) is ACTIVE LOW.** Elecrow's docs say "setting LOW enables audio power
  and HIGH disables it". Naming it from the pin label got this backwards in both directions at
  once — amp powered down when playing, powered up when idle. The amp is gated on just before a
  cue and dropped after a 1.5 s idle timeout, because leaving a class-D amp enabled hisses.
- Deliberately **not** routed through the `localAudio*` HAL contract, which means "play a file off
  local storage" — a thing this unit has no concept of.

### Shared core, unchanged

All 3,127 lines of `src/core/` build and run against Arduino 3.3.11 / IDF 5.5 with **a single
shim**: Arduino 3.x renamed `MDNSResponder::IP(idx)` to `address(idx)`, so `net/registrar.cpp` has
a version-conditional `mdnsResultIp()`. `WiFi`, `HTTPClient`, `Preferences`, `WiFiUDP`,
`ArduinoOTA` and `WebServer` all compiled unmodified.

Runtime-proven on hardware via `jukebox-smoke`, all 8 checks including the two risky ones:

```
[PASS] NVS round-trip   [PASS] wifiConnect()   [PASS] ssdpDiscover()  9 zone(s) in 642 ms
[PASS] GetTransportInfo [PASS] GetVolume       [PASS] GetPositionInfo
[PASS] DIDL parse           fields populated (artUri single-escaped)
[PASS] album art (chunked)  222 KB, JPEG SOI ok — 228,077 bytes in 1353 ms
```

SSDP over ESP-Hosted was proven separately (`jukebox-mcast`, 6/6) **including `udp.begin(1900)`'s
fixed source port**, which was the case most likely to have needed an architecture change.

Two small fixes to shared core came out of this: `registrar.cpp` fell back to `queryHost()` and
refused to cache `0.0.0.0` (the portal was registering as `0.0.0.0:8000` when mDNS returned no A
record), and `selectZoneByIp()` now logs a miss.

---

### Code-review pass (concurrency + network use)

A review of the whole branch for leaks and network over-use found **no heap leak** — every LVGL tree
is cleaned on exit and the art buffers are one-shot. What it did find was concurrency and traffic,
and these are fixed:

- **The health heartbeat was calling `WiFi.RSSI()` / `WiFi.localIP()` / `sonos::zones()` from the UI
  task.** All three are blocking co-processor RPCs or unlocked reads of a vector netTask rewrites.
  netTask now publishes `g_link*` (`core/app.h`) and the heartbeat prints the copy. See the
  heartbeat section above — the diagnostic was inducing the freeze it exists to report.
- **`sonos::zones()` was read from three UI tasks and a board HTTP task with no lock**, while
  discovery did `clear()` + `push_back()` on it. Fixed at the source in `core/sonos/ssdp.cpp`:
  discovery now **builds into a local vector and swaps under a mutex** (the lock is never held
  across network I/O), and `zonesSnapshot()` / `zoneCount()` give other tasks a safe copy.
  `zones()` survives for netTask and is now documented as netTask-only. This was a shared-core bug
  affecting **all three units**, not a jukebox one.
- **Entering Radio re-ran the entire FV:2 browse every single time** — up to 13 SOAP round-trips
  fetching 200 rows to render 40, because leaving the page reset the request flag. Radio → Now
  Playing → Radio three times was ~40 requests in seconds, on the bus that fails under load. Now
  cached for 60 s in plain heap (not the LVGL pool).
- **UI click tones were synthesised on the UI task**, inside `lv_timer_handler()` — up to 180 ms of
  blocking `i2s.write()` on a panel that renders DIRECT into the live scan-out buffer. `uiSoundPlay()`
  now posts to a queue and returns; a board-owned task synthesises and also owns the amp idle
  power-down. That removed the unit's `#include` of a board header (a layering violation) *and* fixed
  the amp being left powered from `boardInit()` through the whole Wi-Fi connect.
- **The dead-link streak had no time window** and was not cleared when Wi-Fi genuinely dropped, so
  two unrelated transient RSSI-0 sightings could add up to a reboot — the exact outcome the
  "requires the symptom twice" rule exists to prevent. Now a 10 s window, cleared on a real
  disconnect.
- **The Rooms grid silently clipped past 18 zones** — fixed 3-column grid in a non-scrollable
  container, no scroll and no notice, unlike the Radio list. Now scrollable (and the chips use
  `lv_obj_set_pos`, since aligned children do not extend a scrollable content area).
- **`registrarTick()` retried two blocking mDNS queries every 45 s forever** when no portal answers,
  stalling netTask's command dispatch each time. Now backs off 45 s → ~12 min, reset on a heartbeat
  failure (where a prompt re-resolve is warranted).


## Known faults

### 🔴 UNRESOLVED — the ESP-Hosted link dies under load

The device runs fine, then `rssi` drops to 0 while `wifi` still reads 3 with a live IP. Sockets
fail, discovery returns nothing, the room list empties. Observed repeatedly, and it correlates
with **load** — the user's description was that room switching "gets slower and slower in
responding as I change rooms, and then I get the searching for rooms".

Matches open, unresolved upstream reports (**espressif/esp-hosted-mcu #167, #121**: random mid-run
transport failures under load).

**Current mitigation — reboot, and it works.** `netLinkRecover()` in `net_link.cpp`. Detection
requires the symptom **twice** so a transient RSSI 0 around a roam or scan never costs a reboot.
Confirmed catching a real failure:

```
[net] RSSI 0 while 'connected' twice — the radio link is dead
[netlink] link is dead and cannot be repaired in place — restarting
rst:0xc (SW_CPU_RESET)
```

> **Do NOT "fix" this by re-initialising the transport.** The first version did the
> apparently-correct thing — `esp_hosted_deinit()` → C6 reset → `esp_hosted_init()` — and it
> **hard-froze the whole device**: no heartbeat, no panic, no watchdog, physical reset required.
> Tearing the transport out from under live lwIP users (artTask mid-download, the registrar, OTA)
> wedges every task. A reboot is safe and deterministic, and since the variant fix it genuinely
> resets the C6 on the way back up.

**Next thing to try: the SDIO bus.** It is currently **1-bit at 10 MHz**, and every album-art
fetch pushes up to 220 KB through it — the failures cluster around exactly that load. Lowering
`CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` is a one-line experiment and signal-integrity faults under
load often improve when you slow the bus. Widening to 4-bit would need D2/D3, which Elecrow does
not document for this board.

**Also still open:** the C6 slave firmware is **2.3.0**; the host stack is 2.12.x and warns that a
version gap can cause RPC timeouts. An SDIO OTA transferred successfully but `Activate` failed
with `ESP_ERR_NOT_SUPPORTED` — 2.3.0 is too old to support the activate RPC, so the image was
written but never marked bootable. Next avenue is flashing the C6 directly over its own UART
(`docs/crowpanel-advance-p4-7in/c6-upgrade/`). **This is a plausible contributor to the fault
above and is the other lead worth pulling.**

### 🟡 Album art occasionally fails to decode

```
[art] drawJpg failed jr=2  hdr=FFD8  544x544  107950 bytes
```

`jr=2` is TJpg's `JDR_INP` — the decoder ran out of input. Valid `FFD8` SOI and well under the
220 KB cap, so neither corrupt nor truncated-by-buffer; the download was cut short, most likely by
a room switch mid-fetch. Cosmetic. The `fix/album-art-truncation` branch adds the short-read
detection that would name this instead of leaving a bare `jr=2`.

Art fetches are now **debounced 700 ms** (`kArtSettleMs` in `album_art.cpp`) so rapid room
switching doesn't start a download per switch — that storm was the real cost of switching rooms,
*not* topology fetches (a switch doesn't call `coordinatorIpFor()` and actually defers the
periodic refresh; an early assumption to the contrary was wrong).

`ART_MAX` is now `#ifndef ART_MAX_PX / 180`, and the `sonos-jukebox` env sets **`ART_MAX_PX=280`** —
a **larger** target than the 180 default, because the design's Now Playing tile is 280 px and 180
is visibly soft at that size. Costs ~313 KB of PSRAM double-buffered (vs ~130 KB at 180), which is
nothing on this board. (An earlier revision of this plan said this unit decoded to a *smaller*
target — wrong in the direction that matters if you are chasing art memory.)

### ✅ SOLVED — the Radio page froze the whole UI

Tapping Radio froze the device: stuck on "Loading favourites", no rail response, **total serial
silence**. It looked like a whole-system hang and was reported twice. It is the **LVGL memory
pool**, exactly as CLAUDE.md warns. Measured:

```
[ui] radio: 70 favourite(s), rendering 40, lvgl_free=73KB
[ui] radio: rendered, lvgl_free=33KB (used 40KB for the list)
```

Each row is 4 objects (button + art tile + glyph + label) at **~1 KB of pool**, so this system's
**70 favourites need ~70 KB of the 73 KB free**. LVGL's response to exhaustion is a **layer-alloc
retry loop**, not a failure — so the UI task spins forever. The serial silence is the tell that it
is the UI task specifically.

First fix was a cap at 40 rows **with an on-screen "showing 40 of 70"** — silently truncating would
be a worse bug than the freeze it prevents.

**The cap was treating the symptom.** `LV_MEM_SIZE` was still the 96 KB sized for the 480x480 nest,
on a board with 32 MB of PSRAM. It is now **512 KB in PSRAM for `UNIT_JUKEBOX` only**
(`include/lv_conf.h`), via LVGL's `LV_MEM_POOL_ALLOC` hook — with `LV_MEM_ADR 0` and no allocator
LVGL declares a *static* array, which would have come out of internal SRAM. Verified in the link
map: `lv_mem_core_builtin.c.o` has zero `.bss` and an undefined reference to `heap_caps_malloc`.
The row cap survives as a backstop at **120** rows, so a real 70-favourite system now renders in
full.

**Budget to keep in mind: ~1 KB of LVGL pool per list row.** Any new full-screen list needs this
arithmetic done before it is written — the pool is bigger now, not infinite, and the S3 units are
still on 96 KB.

### ✅ SOLVED — the C6 wedged on a warm reset (Arduino used the wrong reset pin)

Wi-Fi used to work only on the first boot after a full power cycle. After any P4-only reset the
next boot did this, and every boot after it, until power was removed:

```
E (13050) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x107   (ESP_ERR_TIMEOUT)
rst:0xc (SW_CPU_RESET)   ... loops forever
```

**Root cause: Arduino ignores the ESP-Hosted pins from sdkconfig.** `esp32-hal-hosted.c` seeds
`sdio_pin_config` from the board VARIANT's `BOARD_SDIO_ESP_HOSTED_*` macros, then overwrites the
Kconfig values before calling `esp_hosted_sdio_set_config()`:

```c
conf.pin_reset.pin = sdio_pin_config.pin_reset;   // clobbers CONFIG_..._RESET_SLAVE=32
```

The board JSON used `"variant": "esp32p4"` (Espressif's Function EV Board), whose macros are
CLK/CMD/D0/D1 = **18/19/14/15 — identical to the CrowPanel, which is why Wi-Fi worked at all** —
but `BOARD_SDIO_ESP_HOSTED_RESET 54`, not 32. So the C6 was never reset; a warm P4 reset left it
running a stale session, and `H_TRANSPORT_RESTART_ON_FAILURE` rebooted the host forever.

GPIO32 is confirmed as net **C6_EN** (IC1.EN, 10k pull-up, active high) in Elecrow's Eagle
schematic.

**Fix, and it is durable:** `variants/crowpanel_p4_7in/pins_arduino.h` — a copy of the stock
`esp32p4` variant with that one line changed — plus `board_build.variants_dir = variants`. No app
code calls `WiFi.setPins()`. Verified: two consecutive host resets, Wi-Fi up both times.

> Caveat: setting `variants_dir` makes PlatformIO look for variants **only** in the project
> folder, so any variant named by a board json here must exist under `variants/`.

Two traps for later:
- **Data lines differ by PCB revision.** 7" **V1.0** (ours): d0=14, d1=15, d2=16, d3=17.
  **V1.1/V1.2 reverse them** (d0=17, d1=16, d2=15, d3=14). Reset stays 32 on all. The revision is
  printed on the top silkscreen.
- **GPIO54 is the swappable radio header's NRST** — ESP-Hosted was pulsing it every boot. Harmless
  with the C6 fitted, not a no-op if a LoRa module is ever installed.

Optional hardening, not the fix: `CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n` turns the
unrecoverable boot loop into an `ESP_HOSTED_EVENT_TRANSPORT_FAILURE` event the app can handle.
Worth considering for a wall-mounted unit as a safety net.

---

---

## microSD + on-card caching (in progress)

**Why.** Every track change re-downloads the cover over the ESP-Hosted link — **228 KB in 1353 ms,
measured** — and that link is the one unresolved fault on this board. Caching covers on the card
removes the single largest repeated transfer from the bus that keeps dying. The card is also the
enabler for several other things (boot-state cache, soak logging on a wall-mounted unit with no
serial cable, real design fonts via `lv_binfont_create()` at zero flash cost).

**The enabling hardware fact: the card is on SDMMC *slot 0*, the C6 is on SDIO *slot 1*.**
`CONFIG_SOC_SDMMC_NUM_SLOTS=2` — independent controllers, so card traffic does not share a bus with
the radio. Also `CONFIG_SOC_SDMMC_PSRAM_DMA_CAPABLE=y`, so reads can DMA straight into a PSRAM
buffer (i.e. into the LVGL image buffer, with no bounce through internal RAM).

### Verified on hardware (`jukebox-sd` env, `boards/crowpanel_p4_7in/sd_test.cpp`)

| | |
|---|---|
| Pins | **CLK 43 · CMD 44 · D0 39**, slot 0, 1-bit, **10 MHz** — enumerates first try, no fallback needed |
| Power | **No power-control handle needed.** Do NOT add one — `esp_ldo_acquire_channel(4)` fails here ("already in use by others or not adjustable") |
| Raw writes | `sdmmc_write_sectors` verifies **byte-exact at LBA 2048, 65536, 1 000 000, mid-card and end-of-card** |
| Raw multi-block | 1 → 128 blocks (64 KB) all OK, **~1.2 MB/s** |
| Through FATFS | 156,800 B (one 280x280 RGB565 cover) — **write 950 KB/s, read 722 KB/s** (provisional, see below) |

Against the network path's ~168 KB/s effective, the card is roughly **4x faster per byte**: a cached
cover reads in ~217 ms versus ~900-1350 ms over the C6 link. **The caching direction is justified on
these numbers.**

### 🔴 THE TRAP THAT COST THIS WHOLE SESSION — `CONFIG_FATFS_SECTOR_4096`

The inherited `sdkconfig.defaults` carried `CONFIG_FATFS_SECTOR_4096=y`. That option exists for
**wear-levelled SPI flash**. SD cards have **512-byte sectors** (this one reports
`CSD: ver=2, sector_size=512`), and with the 4096 option FatFs fixes `FF_MIN_SS=FF_MAX_SS=4096` and
**never asks the disk** — so every LBA and transfer length it computes is 8x wrong.

**It does not fail cleanly.** 4 KB writes succeed (exactly one FatFs "sector"); anything larger
becomes a multi-sector transfer that asks the card for 8x the data and dies as
`sdmmc_write_blocks failed (0x107)` — a **TIMEOUT**, which reads exactly like bad hardware. Before
finding it, this session had "eliminated" the card (twice), the pins, the bus clock, the LDO, the
buffer's memory (PSRAM vs internal), the command timeout, and the chunk size.

> **Symptom to recognise: raw `sdmmc_read/write_sectors` flawless across the whole card at every
> size, while everything through `fopen`/`fwrite` times out. That is a sector-size mismatch, not a
> card, a bus or a driver fault.**

Fixed in `platformio.ini`'s `custom_sdkconfig`: `CONFIG_FATFS_SECTOR_512=y`,
`CONFIG_FATFS_SECTOR_4096=n`, plus `CONFIG_FATFS_ALLOC_PREFER_EXTRAM=n` (FatFs hands its own window
buffers straight to SDMMC DMA; internal RAM is not tight here and it removes a class of DMA
alignment questions from the SD path). The fix visibly worked: `f_mkfs` went from dying in 21 ms to
running 1441 ms of real I/O.

### A second trap, not yet stepped in: Arduino's `SD_MMC` library

**Do not use `SD_MMC` on this board as the variant stands.** `SD_MMC.cpp` takes its slot-0 pins,
LDO channel and power pin from the board VARIANT's `BOARD_SDMMC_*` macros, and
`variants/crowpanel_p4_7in/pins_arduino.h` is a copy of stock `esp32p4` — Espressif's EV board —
which declares `BOARD_SDMMC_POWER_PIN 45` / `ON_LEVEL LOW`. **GPIO45 is `PIN_I2C_SDA` here**, the
GT911 touch bus. `SD_MMC.begin()` would pull the touch bus low *and* mount on the wrong pins. Same
class as the C6 reset trap (that variant said reset 54, the board is 32): **stock variant macros
describe Espressif's board, not this one.** Use the IDF `sdmmc` API directly, as Elecrow's own
example does. All of this is documented at the SD block in `boards/crowpanel_p4_7in/pins.h`.

### Where it is blocked

The card in the unit is a 64 GB "ASTC". The block layer is perfect, but IDF's on-device
`esp_vfs_fat_sdcard_format` will not lay down FAT32 on it, and the earlier failed attempts have left
the existing filesystem damaged — so **filesystem-level numbers cannot be trusted until a clean
FAT32 exists**. IDF's FATFS is **FAT32 only**; exFAT support does not exist in IDF at all, and 64 GB
cards ship exFAT by default.

**Next step: format the card FAT32 from a PC** (`mkfs.vfat -F 32`, `format /FS:FAT32`, or Rufus —
Windows will not offer FAT32 above 32 GB from the normal dialog), or drop in a ≤32 GB card, which
formats FAT32 natively everywhere. Then reflash `jukebox-sd` and take the real numbers. Note the
first card tried was **counterfeit** — reported `Name: NCard`, 30 GB, and a write at high LBA
returned `ESP_OK` then read back different bytes (fake capacity). Worth running `f3`/`h2testw` on any
card before trusting it with a cache.

### Design sketch for the cache itself (not yet written)

- **Layering, per CLAUDE.md:** boards own hardware, core owns policy. Add a small blob-store contract
  to `core/board.h` (`blobRead` / `blobWrite` / `blobEvict`) that returns false on boards without
  storage, then have `core/album_art.cpp` consume it. nest and sleep-machine compile against a no-op.
- **Cache the decoded RGB565, not the JPEG** — 280x280x2 = 156,800 B reads straight into the LVGL
  image buffer with no TJpg pass at all.
- **Key on `artist` + `album`, not the art URI.** Sonos's `/getaa?...u=<track uri>` is per-track, so
  hashing it gives a hit only on an exact replay; both fields are already parsed from DIDL, so an
  album key means one fetch per album ever. Store the key string in a header so hash collisions are
  detected rather than rendered.
- **All reads on artTask, never the UI task** — an SD stall on the render task is the same bug as the
  `WiFi.RSSI()` heartbeat above.
- **Eviction** on a counter, not per-write: cap the directory and delete oldest by mtime.

## Open items, in the order I would take them

> **Resolved since this list was written**, so do not re-take them: the album-art truncation fix
> is merged (JPEG_MAX 512 KB); the crawl no longer reboots the device nightly (mbedTLS buffers
> moved to PSRAM — see CLAUDE.md); the dial works; Rooms does grouping; GENA eventing ships for
> Now Playing (plans/09). The heap low-water went 1.4 KB → 50+ KB, which materially changes the
> "link dies under load" picture below — the nightly crawl was a large part of that load.

0. **Wi-Fi provisioning on this unit is UNTESTED.** The path is shared and uniform
   (`appBoot()` → `uiProvisioning()` + `portalRun()`), the jukebox implements the "Join
   `sonos-jukebox-setup`" overlay, `knobDown()` gives it the hold-to-reprovision trigger, and
   `CONFIG_WIFI_RMT_SOFTAP_SUPPORT=y` says the C6 can do AP mode over ESP-Hosted. But nobody has
   watched it happen. Testing it means clearing credentials or holding the dial through power-on,
   and if SoftAP does NOT come up over the hosted bridge the device is off the network with no OTA
   path back. **Do it at the device, with a USB cable to hand.** Not urgent — it only matters on a
   move or a router change — but it is the one path that could strand the unit.

1. **Soak it — and to a written protocol.** Both freezes found so far were found by *using* the
   device, not by reasoning about it, and the link fault only appears under real load. "Soak it" as
   one sentence is not enough: without a fixed procedure the next session improvises one and the
   results are not comparable to today's baseline or to each other. Concretely: scripted room
   switch + track change on a timer (the album-art path is where the failures cluster),
   `readser.py` teed to a file, **pass = N hours with no `netLinkRecover()` line**. Run it once
   before changing anything, so there is a control.
2. **The link fault** (above). Two leads, and I would now take them in this order:
   - **The C6 firmware gap first.** Slave 2.3.0 against a 2.12.x host, with the host stack itself
     warning that the gap causes RPC timeouts. That is a better fit for "RPC dies while the cached
     association survives" than signal integrity is.
   - **Then the SDIO clock.** But confirm what it actually is before spending a board cycle:
     `custom_sdkconfig` sets `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000` while
     `sdkconfig.defaults` carries `CONFIG_ESP_SDIO_CLOCK_FREQ_KHZ=40000`. The generated per-env
     sdkconfigs resolve both to 10000, but that has only been checked for the probe envs — dump the
     effective value for `sonos-jukebox` first. Lowering a number that is not the one in force is
     lesson 1 of this plan wearing a different hat. Note also that 10 MHz is already conservative
     and the album-art path is exactly what a slower bus makes slower.

   This is the one thing standing between "works" and "finished" — a device that reboots itself is
   not shippable.
3. **Detection latency is structural, not a constant to tune.** The RSSI check lives *inside* the
   `recovering` branch, so it only runs after `s_coordStale` (3 failed polls) and then needs a
   second sighting. Meanwhile the heartbeat shows `rssi=0 zones=0` immediately — the user sees the
   dead link before the firmware acts on it. If the soak says that wait is annoying, the fix is to
   check RSSI in the poll path, **not** to lower the streak threshold: the streak (now with a 10 s
   window and a reset when Wi-Fi genuinely drops) is what stops a router reboot from costing a
   device reboot.
4. **Verify free PSRAM on the live nest**, then merge `fix/album-art-truncation`
   (**GitHub issue #3**). The branch raises `JPEG_MAX` 220 KB → 512 KB and adds short-read
   detection. The concern to settle is whether the nest has the PSRAM headroom: the audit says
   album art is already correctly in PSRAM (~7 MB free) and the cap is not an SRAM constraint, but
   that was read from a memory audit, **not measured on the live device**. Measure it.
5. **Favourites rows have no artwork.** Deferred; needs a per-row art fetch strategy that doesn't
   reintroduce the download storm.
6. **External dial — WRITTEN, awaiting hardware.** It does **not** use the 11-pin GPIO header as
   planned here: both controls go on the **shared I2C bus via J13** (Crowtail), so there is no PCNT
   or encoder pin. `boards/crowpanel_p4_7in/knob.cpp` drives an Arduino Modulino Knob (0x76/0x74,
   4-byte read, byte 0 is the pinstrap and NOT data); `units/sonos_jukebox/screens.cpp` binds twist
   to volume and press to play/pause globally. Two things it exposed that outlived it:
   `i2c_bus.{h,cpp}` (whole-transaction lock — the GT911's repeated-start read is not atomic under
   TwoWire's per-call mutex) and a **phantom all-zero I2C response** after any failed transaction,
   which made the driver "find" a dial on an empty bus. Unverified until the cable lands:
   `kCountsPerDetent`, and whether a real Modulino's pinstrap byte is ever 0.

   **4 transport buttons** still to do — a PCF8574 at 0x20 on the same bus, and it must take the
   i2c_bus lock. Still wants a generic HAL, mirroring how wake-word phrases work, where the board
   reports *which* input fired and the unit decides what it means:

   ```c
   // --- Momentary buttons (optional; 0 on boards without any) ---
   int  buttonCount();
   int  buttonPoll();              // index of a press since the last call, else -1
   const char *buttonName(int i);  // "play" | "next" | "prev" | "rooms" — for logs/UI
   ```

   Boards without buttons return 0/-1/nullptr, so nest and sleep-machine stay untouched.
7. **The case.** See below.
8. **Hardware JPEG decode + PPA blits** — available on this silicon, unused. Pure upside, no
   urgency.

## Case notes

`hardware/jukebox-7/`, generated with the existing Python CSG toolchain (trimesh + manifold3d) off
`tokens/hardware.css`. Hardware commits stay separate from firmware.

The screen cutout is essentially already right — the design specifies 154×86 mm and the real active
area is **155×87 mm**. **The face is not:** the design assumes a 210 mm face with a 46 mm control
column beside the screen, but the **PCB alone is 180 × 105 mm**, so the column cannot overlap it.
Either widen the face to roughly 180 + column, or mount the dial and buttons on a separate
sub-panel. Re-derive from the real PCB outline (Elecrow ships a `.stp` model and Eagle files) before
printing anything.

## Open questions

- Does the dial's RGB light-ring ship in v1? It would be the first software-controllable LED in the
  project (`PIN_LED` GPIO48 exists on this board, but the ring is external).
- Camera header is present and unused. Out of scope unless you want presence-wake.
- Accent locked to amber unless you say otherwise.
