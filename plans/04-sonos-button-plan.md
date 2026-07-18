# 04 — sonos-button: a one-button headless Sonos bedtime trigger

**Board:** nulllab / emakefun **ESP32-S3-CAM** (`github.com/nulllaborg/esp32s3-cam`)
**Unit:** `sonos-button` — a wall/shelf appliance with **one physical button**. Press it and the
configured Sonos room starts the configured sleep playlist, looped, at a configured volume.
Press again to stop. All configuration happens in a browser; the device has no screen.

This is the **third unit** in the repo (after `nest` and `sleep-machine`) and the first
**headless** one. It reuses the shared core wholesale — see "Why this is cheap" below.

> ## Status — the device works; the case is printable; only WiFi provisioning is left
>
> **Built, on hardware, verified against real Sonos** (env `sleep-button`, branch
> `worktree-button-measurements`, draft PR #1):
> - **Phases 0–3 done.** Button → playlist toggle, and a config page on `:8080` with ring
>   on/off + dimming, room, playlist (listed live from Sonos), volume, and device name.
> - **The ring needs no MOSFET** — a bare GPIO sinks it low-side (§4). One fewer part, one less
>   thing in the case.
> - **Case built** — `hardware/cam-button/`, 26.81 mm, both meshes watertight (§6).
> - **Phase 5 (WiFi provisioning portal) — built (`core/net/portal.{h,cpp}`), not yet exercised
>   on hardware.** No creds, or the button held through power-on, raises an open AP
>   `sonos-button-setup` with a captive join page; `wifiApply()` persists on success / reverts on
>   failure. HEADLESS-only, so nest/sleep-machine are untouched. **Needs a hardware pass:** clear
>   creds (or hold the button at boot), join the AP from a phone, confirm the captive sheet pops
>   and the box rejoins your network. See §"Phase 5 in detail" for the entry-logic traps.
>
> **Not done:**
> - **Phase 4 (status-LED blink codes)** — mostly moot: GPIO2's LED is real but *not broken out*
>   (§3), so it can only blink inside a closed case. The button's own ring is the real indicator,
>   and it's already driven. This phase shrank to "maybe a boot/fault blink on the ring."
> - **Phase 5 hardware validation** — the code is in; the phone-provisioning round-trip hasn't
>   been run on the real board yet.
> - The button's **long-press** is reserved and does nothing yet (a runtime portal re-open is a
>   natural home for it; today only hold-at-power-on triggers the portal).
> - **Auto-stop timer** — does not exist anywhere in this repo; would be new work, not a port.

---

## 1. Decisions (settled)

| Question | Decision |
|---|---|
| Button behavior | **Toggle.** Press = start sleep playlist (looped). Press again = stop. |
| Playlist source | **Sonos saved playlist (`SQ:`)**, enqueued + `REPEAT_ALL` — the proven path. |
| Volume | **Configurable fixed volume.** Set the room's volume, *then* play. |
| Camera | **Not used.** Unplug the FPC ribbon and remove the module (see §6). |
| Config surface | Embedded web UI on `:8080` — ring, room, playlist, volume, device name. ✅ built. |

### WiFi provisioning — **C is reached in code: A + B both built (B untested on hardware)**

There is **no screen**, so there is no on-device way to type an SSID. Three options:

- **A — `secrets.h` (compile-time).** ✅ **Done.** Works today; changing networks = rebuild +
  USB reflash. Fine for your own house, painful for a taped-down box.
- **B — SoftAP captive portal.** ✅ **Built** (`core/net/portal.{h,cpp}`), ⏳ **not yet exercised
  on hardware.** No creds (or button-held-at-boot) → raise the AP, serve a captive join page,
  persist to NVS via `wifiApply()`. Spec below and in Phase 5.
- **C — Both.** A, with B as fallback + recovery. **This is where the code now sits** — pending
  the hardware pass on B.

**The credential half of B already exists and is proven** (it's how the config page'd Wi-Fi row
would work): `wifiApply(ssid, pass)` in `core/net/wifi.cpp` tries new creds, **persists on
success and reverts to the old ones on failure** so a typo can't strand the device;
`settingsSetWifi()` persists; `g_pending.wifiSsid/wifiPass` carries it across tasks. `DNSServer`
ships with the Arduino core, so **no new dependency**. What's new is the AP + join page + the
entry logic — see the Phase 5 spec below.

> Re-opening the portal after it's configured needs a trigger that can't collide with the
> runtime toggle. Use **hold the button through power-on**: `knobDown()` already exists, GPIO14
> is not a strapping pin so a press at boot is harmless, and `main.cpp` can check it between
> `boardInit()` and `appBoot()`. The reserved **long-press** is the runtime equivalent.

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
- **Flashlight LEDs:** GPIO3 · **Green pilot LED:** GPIO2 — ✅ **CONFIRMED REAL** from the vendor
  schematic (`esp32s3_cam_sch.pdf`, "Extension Interface / LED" block): **IO2 → R6 1 K → D5
  (yellow-green) → GND**, i.e. **active-HIGH**, ~1.2 mA. No hardware test needed; the guess was
  right. ⚠️ **But see below — IO2 is not broken out**, so it can only ever blink *inside* the case.
- **PSRAM/flash (OPI):** 26–37 — never touch

### The actual header pinout — **read off the vendor schematic** (this supersedes guesswork)

Two 8-pin headers, **J4** and **J5**. This is the whole external interface:

| J4 | net | | J5 | net |
|---|---|---|---|---|
| 1 | **+5V** ✅ *(live — measured; the schematic disagrees, see §4)* | | 1 | **+3V3** |
| 2 | **GND** | | 2 | **GND** |
| 3 | IO1 | | 3 | IO46 *(LOG strap)* |
| 4 | IO48 | | 4 | IO41 *(MTDI)* |
| 5 | **NC** | | 5 | IO42 *(MTMS)* |
| 6 | IO47 | | 6 | IO43_TX *(console)* |
| 7 | **IO14** ← the button | | 7 | IO44_RX *(console)* |
| 8 | BOOT *(IO0)* | | 8 | EN |

Plus **J2**, a **PH2.0 4-pin** connector: **1=IO48, 2=IO47, 3=+5V, 4=GND**. (The README's "PH2.0
battery JST" is **J1**, a separate 2-pin — don't confuse them.)

#### ⚠️ Physical order: **J4's pin 1 is at the BOTTOM** — don't wire by counting

The vendor's `picture/esp32s3_cam_pin.png` gives the *physical* layout, which the schematic's pin
numbers do **not** imply. Reading each header top-to-bottom as the board is drawn (USB-C at the
bottom):

```
     LEFT header (J4)              RIGHT header (J5)
     BOOT / GPIO0   <- pin 8       RST            <- (not a J5 net; see note)
     GPIO14         <- pin 7       GPIO44 / U0RXD
     GPIO47         <- pin 6       GPIO43 / U0TXD
     NC             <- pin 5       GPIO42 / MTMS
     GPIO48         <- pin 4       GPIO41 / MTDI
     GPIO1          <- pin 3       GPIO46 / LOG
     GND            <- pin 2       GND
     5V             <- pin 1       3V3
```

**Pin 1 is the bottom pin, nearest the USB-C.** So "J4 pin 6" means sixth counting *up* from the
5 V end — an easy way to wire it wrong, and it cost a debug round-trip when black stayed on GND.
**Wire by neighbour, not by number:** `GPIO47` is the pin **immediately adjacent to `GPIO14`**, on
the side away from BOOT.

Two things this picture confirms independently:
- The legend marks **5 V as a documented power rail** (`PWD: Power Rails (3V3 and 5V)`) — so the
  orphaned `+5V` net in the schematic (§4) is definitively an ERC bug, not a dead pin.
- **Every GPIO on these headers is PWM-capable**, so `GPIO47` can do LEDC dimming for the ring's
  brightness slider (§5).

**Consequences that matter:**

- **The whole product wires to J4 and nothing else**: +5 V (1) and GND (2) for power, IO14 (7) for
  the button, IO47 (6) for the ring. Four pins, one connector, no support components.
- **IO2 is NOT on either header.** It goes only to the onboard D5. So `PIN_STATUS_LED 2` **cannot
  drive the button's ring** — the ring needs a header pin. **IO47 (J4.6) is the one**, proven;
  IO1 (J4.3) and IO48 (J4.4) are the free alternates.
- The free-pin list this plan gave earlier was optimistic: **IO1, IO47, IO48** are the genuinely
  free ones. IO41/IO42 cost JTAG, IO43/IO44 are the console, IO46 is a strap, IO0 is BOOT.

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

**Everything lands on J4** (§3's pinout — one connector does the whole job):

```
   brown ──────►  J4.7  IO14     switch, active-low (internal pull-up)   ✅ proven
   brown ──────►  J4.2  GND      the other switch leg (either brown; no polarity)
   white ──────►  J4.1  +5V      ring + — ⚠️ verify this pin is live, see below
   black ──────►  J4.6  IO47     ring − — the GPIO sinks it. No MOSFET; see below.
```

**The onboard GPIO2 LED is real — and useless to us.** The schematic confirms `IO2 → R6 1K → D5`
(§3), so `pins.h:18-21`'s "unverified guess" was correct. But **IO2 is not broken out to J4/J5**,
so it can only blink *inside* a sealed box. It stays worth blinking during bring-up as a
liveness tell; it cannot be the ring's gate. **Use IO47 (J4.6)** for that — IO1 and IO48 are the
alternates.

### ✅ The ring: **switch the GROUND side from a bare GPIO. No MOSFET.** *(proven on hardware)*

A white LED has a **forward voltage of ≈3.0–3.2 V**, and the ring is specced **5–24 V**, so it
carries an internal series resistor sized for **≥5 V**. A 3.3 V GPIO cannot **source** enough to
light it — you'd leave ~0.1–0.3 V across that resistor. That much of the earlier analysis holds.

**But the pin never needed to source. It only needs to sink.** Put white on the full 5 V and land
black on a GPIO: the pin pulls the ring's cathode to ground, which is precisely the job a low-side
MOSFET would have done. **So the MOSFET is unnecessary** — the ESP32's own output driver *is* the
low-side switch.

```
   J4.1  +5V ──────► white (ring +)
                          │
                        [ring]
                          │
   J4.6  IO47 ◄────── black (ring −)      LOW  -> ring sees 5 V      -> ON
                                          HIGH -> ring sees 1.7 V    -> OFF
                                                  (under Vf, so no current, and
                                                   nothing flows back into the pin)
```

**Verified end-to-end with `sleep-button-bringup`:** each button press toggles the ring, on and
off, cleanly (`presses=8`, `bounces=16` — the same 2-edges-per-press ratio as the button-only
test). The ring lights on the IO47 sweep step and is dark on every other.

**Why this is safe:**
- **Sink current** is ~10–20 mA; an ESP32-S3 pin is good for ~28 mA. Comfortable.
- **Nothing sees 5 V.** When the pin drives HIGH (3.3 V), the ring has only 1.7 V across it —
  below Vf — so it cannot conduct and cannot push current back into the pin.
- ⚠️ **The one rule: always drive this pin, never leave it as an INPUT.** Floating, the node
  drifts toward 5 V and only the pin's ESD clamp stops it at ~4 V — over the 3.6 V abs-max, albeit
  at leakage currents. `pins.h` documents this; the bring-up sets OUTPUT+HIGH before anything else.

**What this buys:** one fewer part, no perfboard or heat-shrink blob to house inside the shell
(§6), and **LEDC PWM still works** — the vendor pinout marks every header GPIO as PWM-capable, so
the §5 brightness slider is unaffected. The ring is the entire UI of a screenless box, and it now
costs zero components.

#### ✅ **J4.1 supplies 5 V — CONFIRMED on hardware. The schematic is wrong.**

**Tested: white → J4.1, black → J4.2, USB plugged in → the ring lights.** Both the 5 V rail and
the white ring are proven. No VBUS tap, no boost, no soldering to pads.

**This contradicts the schematic, so record why and don't re-derive it.** In
`esp32s3_cam_sch.pdf`, `+5V` appears **exactly twice on the whole sheet** — J4 pin 1 and J2
pin 3 — and *nothing drives it*. The USB input is labelled `VBUS`, and the power path is:

```
   VBUS ──►|── D7 (K24 Schottky) ──┐
                                   ├──► VOUT ──► U4 XC6220B331MR-G ──► +3V3
   VBAT ──── Q3 AO3401A (P-FET) ───┘            (Q3's gate is pulled to VBUS via R8 1M, so
              source=VBAT drain=VOUT             USB present => battery disconnected)
```

No `+5V` symbol sits on `VBUS` or `VOUT`, so on the sheet `+5V` is a two-pin island that should
raise a KiCad ERC error. **The board says otherwise: the vendor joins it in the PCB layout and
shipped the sheet with the error.** Treat this schematic as authoritative for *pin assignments*
(J4/J5 in §3 are correct — IO14 was proven) but **not** for power-net connectivity.

> ⚠️ **Worth one more probe:** is J4.1 `VBUS` (5.0 V) or `VOUT` (≈4.7 V, post-Schottky)? The ring
> lights either way and it changes nothing today, but it decides the battery question below.
> Measure J4.1 → GND and read the actual number.

⚠️ **Note for a battery future.** If J4.1 is `VBUS`, it exists **only while USB is connected**,
so on the PH2.0 battery (J1) the ring goes dark — ring and battery operation would be mutually
exclusive without a boost. If it's `VOUT`, the ring follows VBAT (3.0–4.2 V) on battery and would
merely go dim. §6 has the unit permanently USB-powered, so this is a future-proofing note, not a
problem today.

**Design consequences now that the rail is real:**
- **Don't** leave it wired as tested (white→5 V, black→GND). That's an always-on ring with no
  status feedback, which throws Phase 4 away. It was a *test*, not the design.
- The ring wiring is settled and proven: white→J4.1, black→J4.6 (IO47). No MOSFET (see above).
- **Measure the ring's current** while it's lit — it sizes nothing critical (any logic-level FET
  handles it) but it's free to know now, and it confirms the "~10–20 mA" guess above.

Current draw is unstated (the datasheet's "MICROVOLTAGE" is a mistranslation, not a spec).
Expect ~10–20 mA at 5 V — inside a GPIO's ~28 mA sink, but **measure it** to be sure of the margin.

```cpp
// src/boards/esp32s3cam/pins.h
#define PIN_BUTTON        14    // J4.7 — FLM12-FJ-6, either brown; to GND, active-low. PROVEN.
#define PIN_RING_GATE     47    // J4.6 — the white ring's return (black), switched LOW-SIDE by
                                // this pin directly. No MOSFET: a 3.3 V pin can't SOURCE a white
                                // ring specced 5-24 V, but it sinks it fine. LOW=on, HIGH=off.
                                // Ring + (white) sits on J4.1 (+5V). ALWAYS drive this pin --
                                // floating, the node drifts to ~4 V on the ESD clamp. PROVEN.
#define PIN_STATUS_LED     2    // Onboard D5 (IO2 -> R6 1K -> D5, active-HIGH). CONFIRMED real
                                // from the schematic -- but NOT broken out, so it is only ever
                                // visible with the case open. Bring-up liveness tell, not UI.
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
2. **`core/webconfig.cpp`** — add fields to `webConfigApply()`: **`playlist`**, **`volume`**,
   **`ring`** (see below), and (with option B/C) **`wifi`**. `webConfigJson()` gains
   `playlist`/`volume`/`ring` and a `playlists[]` list. This is the right home for it per
   `webconfig.h:6-7` — the board's HTTP server must stay sockets-and-routing only.
3. **`core/settings.cpp`** — add `settingsPlaylist()` / `settingsSetPlaylist()`,
   `settingsVolume()` / `settingsSetVolume()`, and `settingsRing()` / `settingsSetRing()`.
   Mechanical; mirrors `settingsSleepTrack()`.
4. **`platformio.ini`** — new `sleep-button` + `sleep-button-ota` envs.

### Ring brightness from the web page — **make it a level, not a switch**

The ring should be configurable from the config page (§3's `+5V` caveat permitting). Two
decisions worth making deliberately:

**Make it 0–100 %, not on/off.** The ring pin (IO47) is a normal GPIO, so **LEDC PWM** gives
dimming for the same effort as a toggle, and `0` *is* off — a switch is the degenerate case of a
level, so building the level costs nothing extra and can't be retrofitted for free later. This is
a **bedside** device: a white ring at full tilt in a dark bedroom is genuinely unpleasant, and
"off or blinding" is a worse product than a dim glow. Ship the slider.

**⚠️ Do NOT reuse `settingsBrightness()` for it.** It looks like the obvious fit — a screenless
box's "brightness" is the ring, and `backlightSet()` is already a no-op here (§5) — but it
**hard-clamps to 10..100** in both directions (`settings.cpp:18-27`: the getter floors at 10, the
setter clamps). That floor is deliberate LCD-safety on the other two units: it stops anyone
blanking the screen and losing the UI they'd need to un-blank it. **A ring has no such problem
and 0 is a legitimate, desirable state** — so reusing brightness would mean the ring can never be
turned off, which is precisely the thing being asked for. Relaxing the clamp in core to suit this
board would hand the nest/sleep-machine units a way to brick their own UI. **Add a separate
`settingsRing()` (0..100, default 100, no floor).**

Which HAL call drives it is a smaller question: either reuse `backlightSet(pct)` (this plan's
"reuse `knobEvent()` rather than invent `buttonEvent()`" instinct, §5) with the board mapping pct
→ LEDC duty, or add a `ringSet(pct)` that the other two boards stub like they stub `localAudio*`.
**Prefer `backlightSet()`** — it needs no core-header change and "the only light on the device" is
a fair reading of backlight.

> **Interaction with Phase 4:** if the ring is the status indicator *and* the user can set it to
> 0, the blink codes go with it. That's the right call — it's their box — but the "no-WiFi" code
> is the one that explains a device that looks broken, so consider letting **fault** codes ignore
> the setting while **idle/playing** states respect it.

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
| **0** | **Bring-up** (`sleep-button-bringup`) — ✅ **essentially done** | ✅ **GPIO14 + button proven** (§4: 4/4 then 8/8 presses, ~2 bounces/press absorbed by the 30 ms window, no external resistor). ✅ **PSRAM 8 MB + flash 8 MB read back correct** — the 8 MB partition config took. ✅ **GPIO2/D5 confirmed real** from the schematic (but not broken out — case-internal only). ✅ **J4.1 supplies 5 V** and ✅ **the ring switches off a bare GPIO (IO47, low-side) — no MOSFET**, button press toggles it end-to-end. ⏳ Remaining: the **Short/Long held-time** classification (never observed — the 700 ms threshold is still a guess Phase 2 would inherit) and the **ring's current draw** (expected ~10–20 mA vs the pin's ~28 mA sink; nice-to-know, not blocking). |
| **1** | **Headless skeleton** — ✅ **done** | `-DHEADLESS` drops `album_art` (the core's only graphics coupling); stub board + unit. Boots, joins WiFi, discovers Sonos, OTA answers. |
| **2** | **Button → playlist** — ✅ **done** | Debounced GPIO14 → `knobEvent()` → the play/stop state machine. Verified against real Sonos; reads the speaker's actual transport so app-side changes don't desync it. |
| **3** | **Config server** — ✅ **done** | `:8080` page + `webconfig` fields for ring, room, playlist (live from Sonos), volume, device name. |
| **4** | **Status LED** — ⚠️ **mostly moot** | GPIO2's LED is **not broken out** (§3), so blink codes only show inside a sealed case. The button ring is the real indicator and is already driven. Shrinks to an optional boot/fault blink on the ring. |
| **5** | **WiFi portal** — ✅ **built; ⏳ hardware pass pending** | SoftAP + captive portal (`core/net/portal.{h,cpp}`), HEADLESS-only. Entry: no creds *or* button-held-at-boot; a failed connect retries rather than dropping into AP mode. Serves an open `sonos-button-setup` AP with a server-rendered scan+join page on :80, wildcard DNS on :53, and applies creds via `wifiApply()`. Builds clean on all three envs; the phone-provisioning round-trip hasn't been run on the real board yet. |

Test loop per the repo convention: build → flash → you confirm on device → commit + push.
Phase 0–1 needed USB; **Phase 2+ can go over `/ota`** (env `sleep-button-ota`, host
`sonos-button.local`). Phase 5 itself is testable over OTA right up until you clear creds.

### Phase 5 in detail — the initial-config / recovery portal

**Goal.** When the device has no working WiFi, it raises its own access point and serves a page
where you join it to your network from a phone. First-boot config *and* recovery, in one path.

**Entry — get this exactly right; the naive version is worse than no portal.**

- ⚠️ **Never open the portal just because `wifiConnect()` failed.** `beginFromStored()` falls back
  to compile-time `WIFI_SSID`/`WIFI_PASS`, so a configured device *always* has creds to try. If a
  failed connect opened the portal, a **router reboot or a 30-second outage would drop your
  nightstand button into AP mode and it would never rejoin on its own** — a worse failure than the
  one being fixed. A failed connect must **retry**, not give up.
- **Open the portal only when:**
  1. **there are genuinely no credentials** — nothing in NVS *and* no `secrets.h` creds compiled
     in (so this never fires on your own build, which has them); or
  2. **you asked for it** — hold the button through power-on (`knobDown()` between `boardInit()`
     and `appBoot()`), the deliberate recovery trigger.
- Everything else — creds present but the AP is down right now — is a **retry loop**, not a portal.

**The AP + page.**

- `WiFi.mode(WIFI_AP)` (or `WIFI_AP_STA` to scan while serving), SSID `sonos-button-setup`, open
  or a fixed WPA2 pass printed in the README. `softAPIP()` is `192.168.4.1`.
- **Captive-portal auto-popup needs port 80.** Phones probe `http://<gateway>/` on **:80** and pop
  the "sign in to network" sheet on a 200/redirect. Our config server is on **:8080**, which the
  probe never hits — so the portal needs its **own :80 server** (or bind the existing one to :80
  while in AP mode). Without it you'd have to type `192.168.4.1` by hand, which defeats the point.
- **`DNSServer` on :53, wildcard** — answer every name with `192.168.4.1` so any URL the phone
  tries lands on the join page. Ships with the Arduino core; no new dep.
- **Page:** scan SSIDs (`WiFi.scanNetworks()` — needs `WIFI_AP_STA`), a list + password field,
  POST to apply. Keep it one self-contained PROGMEM page like `config_server.cpp`'s.

**Apply — reuse what's proven, don't reinvent it.**

- On submit, call the **existing `wifiApply(ssid, pass)`** (`core/net/wifi.cpp`): it tries the
  creds, **persists on success, reverts on failure**. That revert is exactly right here — a
  mistyped password must not brick the portal. Report the result on the page
  (`wifiApplyResult()`), and on success tear down the AP + DNS and continue to `appBoot()`.
- ⚠️ **`config_server.cpp:174` waits for `WL_CONNECTED` before binding** — that never happens in
  pure `WIFI_AP`. Either the portal owns its own server, or that wait becomes "STA connected *or*
  AP up." Don't let the config task silently never start.

**Scope.** ~200 lines in a new `core/net/portal.{h,cpp}` (device-agnostic — the nest/sleep-machine
could use it too), plus ~15 lines of wiring in `main.cpp`/`appBoot()`. Half a day. `DNSServer`
is the only moving part that's new; everything else is glue over code that already works.

**Honest cost/benefit.** For *your* house with creds compiled in, day-to-day value is low — it
earns its keep only when you change SSID, move the device, or give one away. The reason to build
it is **recovery**: today a WiFi change means peeling a taped-down box off the nightstand and
carrying it to a USB port. The portal makes it fixable from a phone, in place.

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
- Mounting holes are **Ø3.2 with a Ø6.4 pad — textbook M3**, read off the vendor STEP.
  ✅ **This corrects an earlier guess in this plan**, which read the pads as "M2 (Ø2.2), *not* M3
  like the other two boards". They are M3, the same as the ES3C28P — which is why the
  sleep-machine's screws drop straight in. Don't reintroduce the M2 reading.
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

- **Shell** — box body; board drops in and screws to **4 printed bosses** (Ø2.6 pilots for
  **M3×8 flat**) on the 32 × 24 pattern. Lid takes **M3×10 flat** — flush is mandatory under
  adhesive tape. Built: `hardware/cam-button/`.
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
  GND, IO14 and IO47 — all four on J4** (§3), and thanks to the low-side-GPIO trick (§4) there is
  **no MOSFET and no support board to house**. Nothing but wire goes in the shell.
  ⚠️ **You must SOLDER to J4 — DuPont crimps do not fit.** An earlier draft of this plan claimed
  the pre-soldered headers were "actively useful… can land on female DuPont crimps and stay
  serviceable". With the board pins-down there is only **12.81 mm** under the header body and a
  2.54 mm DuPont female housing is **~14.7 mm**. It does not go. The headers still set the height
  (§6); they just don't buy serviceability.
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

## 7. Open items

Most of the original list is closed. What's genuinely left splits into **bench checks before you
print the case** and **one build decision**.

**Firmware — settled.** The button, ring, config page (room/playlist/volume/ring/device-name),
and the Sonos toggle are built and verified on hardware. **Phase 5 (the WiFi portal) is now built
too** (`core/net/portal.{h,cpp}`) — the last piece of code. What remains is a **hardware pass on
it**: clear creds (or hold the button at boot), join `sonos-button-setup` from a phone, and
confirm the box rejoins your network. Everything else is bench checks + a print decision.

**Before printing the case — caliper checks that still move geometry (§6):**

1. **Header pin direction** — up, down, or both? The design *assumes down* (button hangs into the
   space beside the board). The vendor CAD can't answer this; you can, by looking. **This is the
   load-bearing assumption of the whole case** — confirm it first.
2. **Is the FLM12-FJ-6 body 13.35 mm overall or behind-panel?** Errs safe (wastes height, never
   fouls the lid), so it doesn't block printing — but it sets internal Z.
3. **Thread length** — if it's really ~4 mm, the top face needs a thinned ~2 mm pocket at the
   bore. Caliper it.

**Resolved, recorded so they don't come back:** button measurements (FLM12-FJ-6 datasheet, §4/§6);
mounting holes are **M3, not M2** (vendor CAD, §6); **must solder to J4** — DuPont won't fit (§6);
+5 V is live, ring needs no MOSFET, GPIO2 not broken out (§3/§4); camera comes off **for
footprint, not height** (§6).

**The portal is built** — so the remaining call is just **when to validate it on hardware**. It's
the difference between "fixable from a phone" and "peel it off the nightstand and reflash over
USB" when the network changes; the round-trip (clear creds / hold button → join AP → rejoin)
needs one real-board run to sign off. No rush for a one-house device that already has `secrets.h`
creds — the portal is what makes it *giftable* and *recoverable*.
