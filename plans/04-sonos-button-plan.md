# 04 — sonos-button: a one-button headless Sonos bedtime trigger

**Board:** nulllab / emakefun **ESP32-S3-CAM** (`github.com/nulllaborg/esp32s3-cam`)
**Unit:** `sonos-button` — a wall/shelf appliance with **one physical button**. Press it and the
configured Sonos room starts the configured sleep playlist, looped, at a configured volume.
Press again to stop. All configuration happens in a browser; the device has no screen.

This is the **third unit** in the repo (after `nest` and `sleep-machine`) and the first
**headless** one. It reuses the shared core wholesale — see "Why this is cheap" below.

---

## 1. Decisions (settled)

| Question | Decision |
|---|---|
| Button behavior | **Toggle.** Press = start sleep playlist (looped). Press again = stop. |
| Playlist source | **Sonos saved playlist (`SQ:`)**, enqueued + `REPEAT_ALL` — the proven path. |
| Volume | **Configurable fixed volume.** Set the room's volume, *then* play. |
| Camera | **Not used.** Unplug the FPC ribbon and remove the module (see §6). |
| Config surface | Small embedded web UI: room, playlist, volume. Modeled on `sleep-machine`. |

### Open decision: WiFi provisioning

There is **no screen**, so there is no on-device way to type an SSID. Three options:

- **A — `secrets.h` (compile-time).** Zero new code, works today. Changing networks = rebuild
  + USB reflash. Fine for phase 1 / your own house.
- **B — SoftAP captive portal.** No creds in NVS → raise an AP (`sonos-button-setup`), serve a
  join page, persist to NVS. ~200 lines, genuinely standalone.
- **C — Both (recommended).** Try `secrets.h`/NVS; fall back to the portal. Also gives a
  recovery path when the unit can't reach the network.

**Recommendation: build A in Phase 1, add B in Phase 5 to reach C.** The plumbing for B already
exists — `PendingCmds::wifiSsid/wifiPass` (`player_state.h:43-45`) is applied by `processPending()`
at `app.cpp:166-174`, and `settingsSetWifi()` persists it. Only the AP + portal page are new.

> Note: because the button is a plain toggle, re-opening the portal needs a separate trigger.
> Use **hold the button while powering on** (checked once in `boardInit()`), which cannot
> collide with the runtime toggle.

---

## 2. Why this is cheap — the core is already headless-clean

Audited every file in `src/core/`. **Only two files touch graphics:**

- `core/album_art.h:7` — `#include <lvgl.h>` (and leaks `lv_image_dsc_t` into its API)
- `core/album_art.cpp:3` — `#include <TJpg_Decoder.h>`

**Everything else is already `String`/`int`/FreeRTOS only**: `ssdp`, `soap_client`, `didl`,
`library`, `net/wifi`, `net/ota`, `settings`, `webconfig`, `player_state`, `board.h`, `unit.h`.
`board.h:1-3` states the intent outright: *"Declarations only: no LVGL, no board pins, no driver
headers."*

`app.cpp`'s only coupling is three lines: the `album_art.h` include (`:9`), the `artTask` body
(`:243-264`), and its `xTaskCreatePinnedToCore` (`:278`). `artTask` is fully self-contained —
it reads only `g_player.artUri` and calls only `albumArtClear()`/`albumArtFetch()`. Nothing
depends on it running.

`library.h`'s whole API is headless-callable as-is. And `webconfig.cpp` needs **zero** changes to
work here: it reads the track list through the `localTrack*` board HAL, and the crowpanel board
already proves the stub path (`crowpanel_rotary/board.cpp:35-38` returns `0`/`nullptr`, so
`webConfigJson()` just emits `"tracks":[]`).

**Net: no new Sonos code. The work is a board, a small unit, a config page, and a partition line.**

---

## 3. Board facts (from the vendor docs, downloaded and reviewed)

| | |
|---|---|
| SoC | ESP32-S3R8, dual LX7 @ 240 MHz |
| PSRAM | **8 MB OPI** — same as the other units |
| Flash | **8 MB** ⚠️ *(the other units are 16 MB — partition change required)* |
| PCB | **38.4 × 30.4 mm**, 4× mounting holes on a **32 × 24 mm** rectangle |
| USB | Type-C, native USB-CDC — no external programmer |
| Other | TF slot (SDMMC 38/39/40), PH2.0 battery JST + charger, IPEX antenna option |

**Flash is a non-issue.** `default_8MB.csv` gives **3.3 MB per OTA slot**; today's `nest` build is
1.4 MB and `sleep-machine` is 1.9 MB. A headless build (no LVGL, no TFLM, no audio) lands ≈1 MB.
Just set `board_build.partitions = default_8MB.csv` and `board_upload.flash_size = 8MB`.

### Pins already committed on this board

- **Camera (DVP):** 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18
- **TF card (MMC):** 38 (CMD), 39 (CLK), 40 (DAT0)
- **Flashlight LEDs:** GPIO3 · **Green pilot LED:** GPIO2 *(schematic shows an LED on IO2 —
  **VERIFY** on hardware; if real, it's free status feedback)*
- **PSRAM/flash (OPI):** 26–37 — never touch

---

## 4. Button wiring — **use GPIO14**

### The pins actually broken out to the headers

| Pin | Alt function | Usable for the button? |
|---|---|---|
| **GPIO14** | ADC2_3, TOUCH14, **RTC** | ✅ **Best choice** |
| GPIO1 | ADC1_0, TOUCH1, RTC | ✅ Good alternate |
| GPIO47 / GPIO48 | SPICLK_N/P | ✅ OK (no RTC) |
| GPIO41 / GPIO42 | MTDI / MTMS (JTAG) | ⚠️ Works, but costs you JTAG |
| GPIO43 / GPIO44 | U0TXD / U0RXD | ❌ Serial console — keep for debugging |
| GPIO0 | **BOOT strap** | ❌ Has a button already; strapping |
| GPIO46 | **LOG strap** | ❌ Strapping pin, must be low at boot |

### Recommendation: **GPIO14** — ✅ **proven on hardware**

It is a plain GPIO with **no strapping role**, has an **internal pull-up**, is not used by the
camera or the TF slot, is broken out on the header, and is **RTC-capable** — which leaves the
door open to deep-sleep-wake-on-button later if you ever run it off the battery JST.

**Verified with `sleep-button-bringup` on the real FLM12-FJ-6** (the two browns to GPIO14 + GND,
LED ring not connected):

- **4 presses → `presses=4`.** No spurious counts, no missed presses.
- **Idle reads `raw=up`** on the internal pull-up alone. **No external resistor needed.**
- **`bounces=8` across 4 presses** — ~2 raw edges per press, absorbed by the 30 ms window into
  clean events. Ordinary switch chatter, and it **does not climb while untouched**, which is the
  signature that would indicate noise. **The 100 nF cap this section holds in reserve is not
  needed** at this wire length.

> Not yet observed: the per-press **Short/Long** held-time classification (the lines scrolled past
> before the serial reader attached). The 700 ms threshold is still a guess, not a measurement —
> worth confirming before Phase 2 inherits it as the `knobEvent()` contract.

### The button: **FILN FLM12-FJ-6** (datasheet in hand)

| | |
|---|---|
| Model | **FLM12-FJ-6** — "IP67 waterproof tactile thin metal button" |
| Thread / size | **M12 × 0.75**, 12 mm nominal → confirms `BUTTON_BORE_D` 12.0 mm |
| Switch function | **SELF-RESET** = momentary, and the drawing labels the contacts **NO / C** → **normally-open**, exactly what `INPUT_PULLUP` wants. No latching to design around; §1's toggle is done in software, which the datasheet itself says ("if a self-locking function needs to be achieved, it should be controlled through a program"). |
| Termination | **4-pin MX1.25 board-to-wire**, pre-crimped **150 ± 5 mm** 26 AWG PTFE pigtail — **no soldering to the button.** |
| Contacts | **NO**, **C**, **LED+**, **LED−** |
| LED | **5–24 V**, and the ring is **WHITE** ⚠️ — see "the LED is the problem" below. |

### The harness: **black, white, two brown** (as received)

Four wires, and the datasheet's legend decodes all of them without a meter:

| Wire | Is | Why we know |
|---|---|---|
| **brown** | switch **NO** | §4 wiring text: *"connect the positive terminal of the power supply to the brown wire of the switch, and the other brown wire … to the device"* — i.e. **both browns are the switch**. |
| **brown** | switch **C** | ditto — and see the polarity note below. |
| **black** | **LED−** | *"the black wire represents the negative pole."* |
| **white** | **LED+** | *"the colored wires corresponding to the lights indicate the positive pole"* + *"the color of the lamp wire matches the color of the light."* |

**The two browns are interchangeable.** NO/C is a dry mechanical contact with no polarity, so it
does not matter which brown goes to GPIO14 and which to GND. Don't waste a meter session on it.

**White is not a wire color choice — it's the LED's color**, per the datasheet's own rule that
the lamp wire matches the light. That single fact is what makes the section below a plan rather
than a maybe.

### Wiring

```
   brown ──────►  GPIO14        switch, active-low (internal pull-up)
   brown ──────►  GND           the other switch leg (either brown; no polarity)
   white ──────►  LED+  ┐
   black ──────►  LED−  ┘       ring — DO NOT drive from a 3.3 V GPIO; see below
```

**The button's own LED ring supersedes the onboard-LED question.** `pins.h:18-21` flags the
green LED on GPIO2 as an unverified guess from the schematic. It no longer matters much either
way: the button ring is *better* status feedback than an LED buried inside the case — it's the
one thing the user is already looking at. Treat the onboard LED as a bonus if bring-up proves it
exists.

### ⚠️ The LED is the problem: a **white** ring on a **3.3 V** micro

A white LED has a **forward voltage of ≈3.0–3.2 V**, and the ring is specced **5–24 V**, so it
carries an internal series resistor sized for **≥5 V**. Drive it from a 3.3 V GPIO and you leave
roughly **0.1–0.3 V** across that resistor — microamps. **It will be dark, or a useless glimmer.**
This is not the coin-flip an earlier draft of this plan described; that draft hedged on ring
color, and the color is now known to be the bad one.

**So plan for the 5 V path from the start:**

```
        5V rail ──────► white (LED+)
                          │
                        [ring]
                          │
        black (LED−) ───► D
                       ┌──┴──┐
             GPIO2 ───►│ G   │  N-MOSFET, logic-level (2N7002 / AO3400)
                       └──┬──┘
                          S
                          │
                         GND
```

Low-side switching keeps the gate referenced to GND, so a 3.3 V GPIO drives it fine even though
the LED sits on 5 V. **Cost: one part, and the "three pins" becomes four connections** —
GPIO14, GND, 5 V, GPIO2. Worth it: the ring is the entire UI of a screenless box.

- **Confirm the board actually exposes 5 V on a header** before committing — it has USB-C and a
  PH2.0 battery charger, so a 5 V/VBUS pin is very likely, but ⚠️ **VERIFY** it.
- **Don't** just tie white→5 V and black→GND. That "works" and is tempting, but it's an always-on
  ring with no status feedback, which throws Phase 4 away entirely.
- **Still worth 30 seconds in Phase 0:** poke white straight onto GPIO2 and look. If it somehow
  lights usably, take the win and drop the MOSFET. Expect it not to.

Current draw is unstated (the datasheet's "MICROVOLTAGE" is a mistranslation, not a spec).
Expect ~10–20 mA at 5 V — trivial for the MOSFET, but **measure it in Phase 0**.

```cpp
// src/boards/esp32s3cam/pins.h
#define PIN_BUTTON        14    // FLM12-FJ-6 — either brown; momentary to GND, active-low
#define PIN_STATUS_LED     2    // FLM12-FJ-6 white ring, via a low-side MOSFET off 5V.
                                // NOT a direct drive: the ring is white (Vf ~3.1 V) and
                                // specced 5-24 V, so 3.3 V will not light it.
```

```cpp
pinMode(PIN_BUTTON, INPUT_PULLUP);   // idle = HIGH, pressed = LOW
```

**No external resistor required** — the internal pull-up (~45 kΩ) is sufficient for a button on
short wires. If the run to the button is long (>30 cm) or the environment is noisy, add a **100 nF
cap across the button legs** for hardware debounce. Software debounce (~30 ms) is in the driver
regardless.

**Wiring note:** GPIO14 must not be held LOW during flashing — it isn't a strapping pin, so a
pressed button at boot is harmless. That's exactly what makes it safe to overload as the
"hold at power-on = re-open WiFi portal" trigger.

---

## 5. Software plan

### Layout (follows the existing board/unit split exactly)

```
src/boards/esp32s3cam/
    pins.h            # PIN_BUTTON 14, PIN_STATUS_LED 2
    board.cpp         # boardInit(): button GPIO + debounce; HAL stubs for the rest
    button.cpp/.h     # debounce + short/long classification (mirrors pcf8574.cpp's shape)
    config_server.cpp # :8080 WebServer — /, GET|POST /api/config; localManagerUrl()
    status_led.cpp    # optional: GPIO2 blink codes (booting / no-wifi / playing)
src/units/sleep_button/
    unit.cpp          # uiInit() = no-op; uiTick() = poll button + drive the play state machine
```

### The board's `board.h` obligations

The HAL is wide; a headless board stubs most of it (the crowpanel board already does this for
audio/SD, so the pattern is established):

| Function | Implementation |
|---|---|
| `boardInit()` | button pinMode + LED; start config server; **return true** |
| `backlightSet()` | no-op |
| `encoderDelta()` | `return 0` |
| `knobEvent()` / `knobPressed()` / `knobDown()` | **the physical button** — reuse this, don't invent a new HAL call |
| `localAudio*` | no-op / `false` |
| `wakeWord*` | no-op / `false` / `nullptr` / `0` |
| `localFileUrl()` | `nullptr` |
| `localManagerUrl()` | `"http://<ip>:8080"` |
| `localTrack*` | `0` / `nullptr` |

> **Reuse `knobEvent()` rather than adding `buttonEvent()`.** The knob HAL is already
> "a press-classified momentary button" (`board.h:22-27`, Short/Long) and is exactly the
> right shape. No core change, and the `sleep_button` unit reads it the same way `sonos_nest`
> does.

### The unit: `uiTick()` is the whole app

`unit.h` is LVGL-free (`unit.h:6-7` — just `uiInit()`/`uiTick()`), so a headless unit is a
first-class citizen with **no `#ifdef`s in `app.cpp`**. `uiInit()` does nothing; `uiTick()` runs
at the same 5 ms cadence `uiTask` already provides and hosts a small state machine:

```
Idle ──button──► SetVolume+Browse("SQ:") ──results──► match name ──► requestPlay(idx) ──► Playing
Playing ──button──► g_pending.setPlay = 0 (stop) ──► Idle
```

The matching logic is a near-copy of the sleep-machine's Bedtime path
(`units/sleep_machine/screens.cpp:219-227` to kick off, `:1289-1300` to poll `takeResults()` and
match by name, with a 20 s timeout at `:1302`). Set `library::setLoopMode(true)` once in
`uiInit()`. Volume goes via `g_pending.targetVolume`, which `processPending()` applies to the
**speaker** before play, while transport goes to the **coordinator** (`app.cpp:118-126`) —
that group-coordinator distinction is already handled; don't re-derive it.

### Core changes required (small, additive)

1. **`app.cpp` / `main.cpp`** — exclude `album_art` from headless builds. Guard the include
   (`app.cpp:9`), `artTask` (`:243-264`), its task create (`:278`), and `main.cpp:14`/`:72`
   (`albumArtInit()`), behind a `-DHEADLESS` flag; drop `+<core/album_art.cpp>` from
   `build_src_filter`. `main.cpp:10` already establishes this exact guard idiom.
2. **`core/webconfig.cpp`** — add three fields to `webConfigApply()`: **`playlist`**, **`volume`**,
   and (with option B/C) **`wifi`**. `webConfigJson()` gains `playlist`/`volume` and a
   `playlists[]` list. This is the right home for it per `webconfig.h:6-7` — the board's HTTP
   server must stay sockets-and-routing only.
3. **`core/settings.cpp`** — add `settingsPlaylist()` / `settingsSetPlaylist()` and
   `settingsVolume()` / `settingsSetVolume()`. Mechanical; mirrors `settingsSleepTrack()`.
4. **`platformio.ini`** — new `sleep-button` + `sleep-button-ota` envs.

### Config web server (~120 lines)

Lift the *shape* of `boards/es3c28p/local_stream.cpp` and drop the media half:

**Reuse verbatim:** the `serverTask` pattern — wait for WiFi *inside the task* (`:411-427`,
because `boardInit()` runs before `appBoot()` connects), then `begin()`, then a
`handleClient()` + `vTaskDelay(2)` loop pinned to **core 0**; the route-table idiom (`:430-441`);
`handleConfigGet`/`handleConfigSet` (`:239-250`, pure `webconfig.h` delegation, zero SD refs);
`sendJson`/`sendError`; `localManagerUrl()` (`:448-453`); and the embedded-PROGMEM-HTML pattern
(`kIndexHtml`, ~6.5 KB, served with `send_P`).

**Do not carry over:** the entire upload subsystem on :8081 (`:116-237` — that's most of the
file's complexity and is 100% SD-writing), `handleMedia`, `handleList`, `handleDelete`,
`sd_card.h`/`SD_MMC.h`, and the `?v=` cache-buster. **One port (8080) only** — which also means
**no cross-origin problem**, so the CORS/preflight workaround the sleep-machine needs does not
apply here.

Task stack can drop from 8 KB to **4 KB** (the 8 KB there is because `SD_MMC`'s stack sits on top
during uploads — no SD here).

### `platformio.ini` env

```ini
[env:sleep-button]
board_build.partitions   = default_8MB.csv     ; 8 MB part, not 16
board_upload.flash_size  = 8MB
build_flags =
    ${env.build_flags}
    -DUNIT_BUTTON
    -DHEADLESS
    -DDEVICE_HOSTNAME='"sonos-button"'
build_src_filter =
    -<*>
    +<main.cpp>
    +<core/>
    -<core/album_art.cpp>          ; LVGL + TJpg; no display on this unit
    +<boards/esp32s3cam/>
    +<units/sleep_button/>
lib_deps =
    bblanchon/ArduinoJson@^7.1.0   ; no LVGL, no Arduino_GFX, no TJpg, no audio, no TFLM
```

Note `${env.build_flags}` carries `-DLV_CONF_INCLUDE_SIMPLE`, which is harmless without LVGL.
`DEVICE_HOSTNAME` is `sonos-button` — distinct from `sonos-nest`/`sonos-sleep`, so the three
units don't collide on mDNS/OTA (UDP 3232).

### Phases

| # | Phase | Deliverable / proof |
|---|---|---|
| **0** | **Bring-up** (`sleep-button-bringup`) — 🟡 **part done** | ✅ **GPIO14 + the button are proven** (§4: 4/4 presses, clean debounce, no resistor needed). ⏳ Still open here: the **Short/Long timing**, the **PSRAM/flash banner** (8 MB partition sanity — the boot print hasn't been captured yet), and the LED work below. Blink GPIO2, print GPIO14 transitions over USB-CDC. Proves the board, the pin choice, and the LED-on-IO2 guess **before** any app code. **Also settles the FLM12-FJ-6 ring (§4):** confirm the board exposes **5 V** on a header, measure the ring's draw, and — 30 seconds, expect failure — poke white straight onto GPIO2 to see whether a **white** (Vf ≈ 3.1 V) ring specced 5–24 V does anything at 3.3 V. Assume it doesn't and that the MOSFET is real; this phase is where that's cheap to find out. |
| **1** | **Headless skeleton** | `-DHEADLESS` guards + stub board + no-op unit. Boots, joins WiFi (`secrets.h`), discovers Sonos, OTA answers. **No button yet.** Proves the core is portable. |
| **2** | **Button → playlist** | Debounced GPIO14 → `knobEvent()` → the play/stop state machine. Hardcode room/playlist/volume. **The device does its job.** |
| **3** | **Config server** | :8080 page + `webconfig` fields for playlist/volume. Room picker comes free. |
| **4** | **Status LED** | GPIO2 blink codes: booting / no-WiFi / playing. Only real feedback on a screenless box. |
| **5** | **WiFi portal** *(optional)* | SoftAP + captive portal; hold-button-at-boot to re-open. Reaches option C. |

Test loop per the repo convention: build → flash → you confirm on device → commit + push.
Phase 0–1 need USB (`usbipd attach`, download mode on first flash); Phase 2+ can go over `/ota`.

---

## 6. Case plan — `hardware/cam-button/`

Follows the established toolchain: **Python CSG (trimesh + manifold3d)**, not OpenSCAD;
`conda run -n img23d python build_all.py`; a `*_params.py` + `build_*.py` + `render_preview.py`
per part. Keep hardware commits **separate from firmware** (`hardware/README.md`).

```
hardware/cam-button/
    README.md            # board spec + VERIFY flags
    esp32s3_cam_sch.pdf  # vendor schematic (downloaded)
    shell/
        button_params.py  build_shell.py  build_lid.py
        render_preview.py  build_all.py
        shell.stl  lid.stl
```

### Board spec (from the vendor drawing)

- PCB **38.4 × 30.4 mm**, corner radius ≈ **R2** *(VERIFY)*
- **4× mounting holes** on a **32 × 24 mm** rectangle — i.e. **3.2 mm in from each edge**
- Hole diameter **not dimensioned** on the drawing. The pads read as **M2 (Ø2.2)**, *not* M3 like
  the other two boards — **caliper-check before printing bosses.** ⚠️ **VERIFY**
- **USB-C is centered on one short (30.4 mm) edge**
- BOOT and RESET buttons flank the USB-C on that same edge
- **`BOARD_STACK_T` = 14.5 mm** — measured, thickest point, driven by the **pre-soldered header
  pins**. ⚠️ **Removing the camera does not reduce this**; the headers are the tall part. Any Z
  figure taken from the vendor's 2D outline drawing is the bare PCB and will mislead you.
- Vendor publishes a **3D STEP/model** (`releases/download/V0.0.1/esp32s3_cam_3d.zip`) — pull it
  for the *lateral* keep-outs, but the measured 14.5 mm governs Z (the model almost certainly
  omits the headers). ⚠️ **VERIFY which way the pins protrude** — see §6.

### Design

**Two parts: shell + lid.**

- **Shell** — box body; board drops in and screws to **4 printed bosses** (Ø1.7 pilot for M2
  self-tappers, pending the hole-Ø verify) on the 32 × 24 pattern.
- **USB-C slot** — a **generous cutout** on the short edge, oversized to clear chunky cable
  overmolds. This is power-only and permanent, so size it for the actual cable you'll use, not
  the connector. Keep BOOT/RESET reachable through the same face if you can — it costs nothing
  and saves you a disassembly during bring-up.
- **Button** — a bore on the **top face** for the **FILN FLM12-FJ-6** (see §4 for the electrical
  side). Datasheet drawing + your calipers:

  | Param | Value | Source / note |
  |---|---|---|
  | `BUTTON_BODY_D` | **11.71 mm** | measured — matches the datasheet's **M12 × 0.75** thread (11.71 is the thread's measured major Ø, slightly under the 12 nominal, as expected). |
  | `BUTTON_BORE_D` | **12.0 mm** | = thread + ~0.3 mm clearance. FDM prints holes **undersize**; a bore modelled at 11.71 will not accept the button. Tune on a test coupon before committing the shell print. |
  | `BUTTON_HEAD_D` | **14.0 mm** | datasheet ø14 head — the flange that sits *on* the top face and hides the bore. Bore tolerance is forgiving: the head covers ~1 mm of slop all round. |
  | `BUTTON_NUT_AF` | **16.0 mm** | datasheet — hex **across-flats**. ⚠️ **This is the nut OD you asked about.** Across-*corners* is ≈ 16/cos30 ≈ **18.5 mm**, so the bore needs **≥18.5 mm of flat, obstruction-free interior** around it, and more if you want a wrench rather than fingers. Nothing — no boss, no rib, no wall — may come within ~9.5 mm of the bore axis on the inside. |
  | `BUTTON_BEHIND_T` | **13.5 mm** *(disputed — see below)* | your measurement, called behind-panel. |

  ⚠️ **The datasheet and your 13.5 mm may be measuring the same thing.** The drawing dimensions
  the body as **13.35 ± 0.1**, which is within 0.15 mm of your 13.5 — suspiciously close for two
  supposedly different quantities. If 13.35 is the **overall** length (head included) and the head
  is the drawing's **1.5 mm**, then the true behind-panel depth is ≈ **11.85 mm**, not 13.5.
  I can't resolve this from a photo of the drawing. **Please caliper it directly: seat the button
  in a 12 mm hole in something flat and measure from the panel surface to the back of the
  connector.** Note this errs safe — designing for 13.5 when it's really 11.85 wastes 1.65 mm of
  height rather than fouling the lid — so it does not block the shell, but it should be settled
  before the STL is final.

  ⚠️ **Max panel thickness is the constraint to watch.** The drawing shows the threaded section
  as only ~**4 mm** long (consistent with the product being sold as a *thin* button). Minus a
  ~2 mm nut, that leaves roughly **2 mm of panel** — *thinner than a typical printed wall.*
  **Caliper the thread length.** If it really is ~4 mm, the top face must be **locally thinned to
  ~2 mm at the bore** (a recessed pocket around the hole), which is the "local thin boss" this
  plan predicted. Set it as `BUTTON_PANEL_T` and derive the pocket from it.

  **What the depth costs us — the button drives the shell, not the other way round.** The body
  intrudes ~12–13.5 mm straight down from the inside of the top face, and behind *that* sits the
  **MX1.25 connector plus its mating shell and the pigtail's bend radius**. The pre-crimped
  harness is good news electrically (no soldering, §4) but it is **worse than solder lugs
  dimensionally** — a mated 1.25 mm housing plus a survivable wire bend wants **~8–10 mm**, and
  unlike a solder joint you cannot dress it flat. Budget:

  ```
    13.5  body behind panel   (or ~11.85 — pending the caliper check above)
  +  ~9   mated connector + pigtail bend radius
  ─────
    ~23   mm of clear interior height under the bore
  ```

  …and the board is **not** the ~5 mm slab an earlier draft of this plan assumed. **Measured:
  `BOARD_STACK_T` = 14.5 mm at its thickest, because the board ships with pre-soldered header
  pins.** The camera coming off does *not* recover this — the headers, not the lens, are the
  tall thing.

  **This makes siting the bore beside the board the whole ballgame.** The two are no longer a
  tall part next to a flat one; they are two tall parts, and whether they overlap in *plan* view
  decides the height of the product:

  ```
  bore BESIDE the board     interior Z = max(23, 14.5)  = ~23 mm   ✅
  bore OVER the board       interior Z = 23 + 14.5      = ~37.5 mm ❌  (button hangs from the
                                                                        lid, board sits on the
                                                                        floor — they stack)
  ```

  A 14.5 mm difference is the box looking deliberate versus looking like a brick. So:

  - **Site the bore off the PCB footprint** — over free floor area beside the board, so the
    button and its connector hang down *next to* it. **Strongly preferred.** It costs floor area,
    which is cheap, instead of height, which is not. Floor grows to roughly **38.4 + gap + 18.5
    ≈ 60 mm** in one axis — fine for a shelf/wall appliance, and it also keeps the pigtail's
    150 mm of slack off the PCB and the nut's 18.5 mm across-corners circle clear of the screw
    bosses.
  - **Raise the top face** to ~37.5 mm above the floor. Simple and compact in plan, but the box
    becomes noticeably tall to suit one button.

  **Don't reach for desoldering the headers to get the board thin.** It doesn't win: even at a
  bare ~5 mm, a bore *over* the board still needs ~28 mm of interior, which is worse than
  **23 mm** from just moving the bore sideways and keeping them. And the headers are actively
  useful here — the FLM12-FJ-6's three connections (§4) can land on female DuPont crimps and
  stay serviceable, instead of being soldered to pads that a tug on the pigtail could lift.

  This is a **hard floor on the shell's internal Z**, so `BUTTON_BEHIND_T` and `BOARD_STACK_T`
  both belong in `button_params.py` *upstream* of the shell height, which should be derived as
  `max()` / `sum()` of them per the layout above rather than set independently.

  ⚠️ **Check which way the headers face before fixing the floor plan.** If the pins protrude
  *below* the PCB, the mounting bosses must be tall enough to keep them off the floor (and the
  14.5 mm is measured pin-tip to pin-tip); if they stand *above* it, the bosses can be short but
  the 14.5 mm eats headroom under the lid. Same number, different consequence for the design.

  Everything downstream is `BUTTON_BORE_D` / `BUTTON_BEHIND_T` / `BUTTON_PANEL_T` /
  `BUTTON_NUT_AF` in `button_params.py`.
- **Button wiring** — the FLM12-FJ-6 ships a **4-wire MX1.25 pigtail, 150 mm** (black / white /
  two brown), so there is nothing to solder *at the button*. The four wires land on **GPIO14,
  GND, 5 V and GPIO2**, with a **low-side MOSFET on the white ring's return** — see §4, and note
  that little board needs somewhere to live. **Leave room for a small perfboard or a blob of
  heat-shrink near the header end**; it's one SOT-23 and nothing else, but it is not zero volume.
  150 mm is also far more slack than a ~60 mm box needs: leave a **channel or a pair of posts to
  coil the excess**, and a **strain-relief rib** at the header end so tugging the button can't
  lift a pad. The button itself is captive in its nut, so the rib protects the *board*.
- **Lid** — screwed or snap-fit; whichever the other parts in `hardware/` favor.

### Camera: unplug it

The camera sits on an **FPC ribbon into a connector** — it lifts out without desoldering, and the
unit never reads it (§1). **Still recommend removing it and designing the case with no lens
boss.** If you'd rather keep the option open, say so and I'll leave a blanked-off lens pocket.

> **The height argument for removing it is dead — take it for the footprint instead.** An earlier
> draft called the camera "the tallest thing on the board" and expected its removal to make the
> shell materially thinner. The measured **14.5 mm header stack** (§3) is what sets Z, and pulling
> the lens does not touch it. Removing the camera now buys **lateral clearance, a simpler top
> face, and one less thing to rattle** — real, but not height. Don't let this plan claim
> otherwise; it's exactly the kind of stale rationale that survives into a design nobody
> re-derives.

### Antenna

Board defaults to the **onboard PCB antenna**; switching to IPEX means moving a 0 Ω resistor.
Keep the PCB antenna and **leave that corner of the shell free of infill-heavy geometry** — don't
bury it against a screw boss. If range disappoints in the final spot, the IPEX mod is the fix.

---

## 7. What I need from you

1. ~~**Button measurements** — bore Ø, body depth, panel thickness, nut OD~~ ✅ **answered by the
   FILN FLM12-FJ-6 datasheet** (§4/§6): M12 × 0.75 → 12.0 mm bore, ø14 head, 16 mm hex nut
   (18.5 across corners). Two **caliper checks** remain, and both change the shell:
   **(a)** is 13.35/13.5 mm the overall length or the behind-panel depth? **(b)** how long is the
   threaded section — if it's really ~4 mm, the top face needs a thinned pocket at the bore.
2. **Caliper the board's mounting-hole Ø** (M2 vs M3) — the drawing omits it.
3. **Which way do the header pins protrude** — above the PCB, below it, or both? The 14.5 mm
   stack (§3) is settled; this decides whether it costs boss height or lid headroom (§6).
4. **A decision on WiFi provisioning** (§1) — or just take the recommendation: `secrets.h` now,
   portal in Phase 5.
5. **Confirm the camera comes off** — for footprint, not height (§6).
