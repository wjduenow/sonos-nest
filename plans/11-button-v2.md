# 11 — button-v2: the sonos-button on a Seeed XIAO ESP32S3

**Board:** Seeed Studio **XIAO ESP32S3** (`seeed_xiao_esp32s3`, ships with the pinned platform)
**Unit:** `units/sleep_button/` — **the same unit as `sonos-button`, unchanged**
**Env:** `button-v2` · **Unit id:** `button2` · **Hostname:** `sonos-button2`
**Case:** `hardware/button-v2/`

Same product as **`04-sonos-button-plan.md`**: press the button, the configured Sonos room starts
the configured saved playlist looped at the configured volume; press again to stop. Double and
triple press each run their own slot. Headless, configured on `:8080`, readable on `:2323`.

Nothing about the behaviour changes. This is a **board swap**, done to get a cheaper part and a
smaller box.

> ## Status
>
> **Phase A (firmware) — done, build-verified, CI-gated.** All five app envs build. Nothing in
> `core/` changed except the unit-id ladders.
> **Phase B (hardware bring-up) — RUN, on real hardware, against real Sonos (2026-08-19).**
> ESP32-S3 rev v0.2, MAC `e0:72:a1:f8:47:3c`. Verified:
> - **Button GPIO8 + ring GPIO9 both correct.** 4 presses, one PRESS + one RELEASE each, no
>   repeats. `held=` 174/184/200/210 ms, all classified Short. Ring toggles on every press, so
>   the low-side drive and the 5V pad are both good.
> - **`bounces=8` for 4 presses = exactly 2 raw edges per press**, all absorbed by the 30 ms
>   window, and `bounces=0` across ~100 s of idle. That is the *same* figure the ESP32-S3-CAM
>   bring-up measured, so `DEBOUNCE_MS` 30 is right for this switch too — inherited, now earned.
> - **App runs**: Wi-Fi up (`rssi -59`), SSDP found all 9 zones, coordinator resolved, 15
>   playlists published to `:8080`, portal registration accepted.
> - **`unit=button2` / `board=xiao_esp32s3` confirmed on the live portal**, alongside the v1's
>   `button`/`esp32s3cam`. The cross-flash hazard in §3 is closed in practice, not just in theory.
> - **Heap 239 KB free / 219 KB min** — in line with the sleep-button's ~243 KB, and miles clear
>   of the ~15 KB floor where LWIP starts failing.
>
> - **END TO END, CONFIRMED BY THE OWNER:** the button starts and stops the configured playlist
>   on the configured room. The unit is doing its job on real hardware.
>
> - **GPIO21 LED polarity settled: ACTIVE-LOW**, as `pins.h` assumed. No code change needed.
>   Verified without a reflash — see §2.
>
> **Still open in Phase B:** a hold long enough to exercise `LONG_PRESS_MS`, and RSSI with the
> antenna inside a *closed* case (the case is not printed yet).
> **Phase C (case) — done, both STLs watertight.** `hardware/button-v2/`, 36.78 × 29.00 ×
> **22.96 mm, 24.5 cm³** against the cam-button's 57.1. Not yet printed.
> The button was **calipered at 14.0 mm tip-to-tail** (2026-08-19), settling `04` §7.1a and
> taking 3.35 mm off the height — see that unit's README §2.

---

## 1. Why not the XIAO ESP32C6

The C6 started this — it is the cheapest XIAO (~$5 vs ~$7) and it is the only one with an
**onboard ceramic antenna**, which in a sealed box next to a metal button is a real advantage.

It was rejected because it is **different silicon**, and this repo already knows exactly what
that costs (see `07-sonos-jukebox.md`). Four workstreams, all of which the S3 deletes:

| the C6 would have cost | the XIAO ESP32S3 |
|---|---|
| A second **pioarduino** platform base in `platformio.ini` — the fork that publishes itself under the name `espressif32`, which `[env]`'s pin exists to defend against. Unpinning either side silently retargets `nest` and `sleep-machine` to Arduino 3.x | `seeed_xiao_esp32s3.json` already ships in the pinned `espressif32@6.9.0`. `board =` is one line |
| **Patching `core/app.cpp:688`.** The C6 is single-core, and `xTaskCreatePinnedToCore(uiTask, …, 1)` trips `configASSERT(taskVALID_CORE_ID(...))` in the IDF's `freertos_tasks_c_additions.h:163`. That is a `core/` change touching every unit, to serve one board | dual-core Xtensa; `core/` runs unmodified |
| A **custom 4 MB partition table**, plus a real flash-budget risk: `sleep-button` is 1,072,864 B today against 1.25 MB stock OTA slots, and IDF 5.5 makes binaries bigger, not smaller | 8 MB flash and `default_8MB.csv` — literally the same two lines `[env:sleep-button]` already carries. Measured: **1,062,093 B, 31.8% of the slot** |
| Rewriting the ring driver for the Arduino 3.x LEDC API (`ledcSetup`/`ledcAttachPin` are gone) | `board.cpp` is a near-copy of `esp32s3cam/board.cpp` |

The XIAO S3 is also the **same ESP32-S3R8 the nest runs** — same 8 MB OPI PSRAM, same toolchain,
same package tree. `tools/pio` needed no change at all.

**What it costs, honestly:** the XIAO ESP32S3 has **no onboard antenna**, only a u.FL connector
and a detachable antenna in the box. That antenna now lives inside a 29 mm-wide case about 10 mm
from a Ø14 metal button body. The cam-button lives with its etched PCB antenna ~4 mm from the same
button and works, so this is a **to-verify, not a blocker** — but it is the one thing the swap
genuinely regresses, and it is Phase B step 5.

The **XIAO ESP32C3** was also considered and is strictly worse for this repo: onboard antenna and
supported by the pinned platform, but single-core RISC-V with 4 MB flash and no PSRAM, so it
carries the `app.cpp` and partition problems anyway.

---

## 2. Pins

From the Arduino core's own variant header, which is what the build actually compiles against:
`~/.platformio/packages/framework-arduinoespressif32/variants/XIAO_ESP32S3/pins_arduino.h`.

| role | pad | GPIO | why |
|---|---|---|---|
| ring gate (low-side) | **D10** | 9 | right-hand rail, beside 5V/GND |
| button | **D9** | 8 | right-hand rail; GPIO1–9 are RTC-capable, so deep-sleep wake stays open |
| status LED | — | 21 | `LED_BUILTIN`, onboard only — **active-LOW**, verified on hardware |

**Avoid:** **GPIO3 (D2) is a strapping pin** (JTAG source select) — the one strapping pin that is
on a pad. **GPIO43/44 (D6/D7) are UART0.** GPIO0/45/46 are not on the pads. Everything else on
D0–D10 is free.

### The wiring

All four wires land on the **right-hand rail**, whose order from the USB-C end is
**5V, GND, 3V3, D10, D9** — so they span ~7.6 mm, the harness has one exit, and the whole left
rail plus the far edge stays clear for the u.FL antenna.

```
5V   -> white  (ring +)
GND  -> brown  (switch)
D10  -> black  (ring -, switched low-side by the pin itself — no MOSFET)
D9   -> brown  (switch)
```

> ⚠️ **3V3 sits between GND and D10.** The four wires are *not* four consecutive pads, and that is
> the easiest mistake to make on this board. Count 1, 2, skip 3, 4, 5.

> ⚠️ **The ring is LOW-SIDE and the gate pin must always be driven.** Identical to the
> ESP32-S3-CAM's GPIO47 (`04` §4): the ring is white (Vf ~3.1 V, specced 5–24 V) so a 3.3 V pin
> cannot source it, but it can sink the cathode. As an *input* the node floats toward 5 V and only
> the ESD clamp stops it at ~4 V, over the 3.6 V abs-max. `boardInit()` drives it HIGH before
> enabling the output; the unavoidable exposure is reset → `boardInit()`, which the cam-button
> has lived with since Phase 0.

> ✅ **LED polarity: ACTIVE-LOW, verified on hardware 2026-08-19.** It needed no reflash and no
> bring-up run, because the app pins the state itself: `boardInit()` calls `statusLed(true)` once
> and nothing touches GPIO21 again, so a running unit holds the pin at the "on" level forever —
> the LED being lit *is* the measurement, and a dark LED on a healthy board would have meant
> active-HIGH. Worth remembering as a technique: a constant-state output is self-reporting, and
> reaching for the bring-up first would have cost two flashes to learn the same thing.
>
> The original reasoning, kept because it is why the guess was worth flagging at all:
> ⚠️ XIAO boards conventionally wire the user LED **active-LOW**, the
> opposite of the ESP32-S3-CAM's active-HIGH D5. `pins.h` declares
> `PIN_STATUS_LED_ACTIVE_LOW 1` and routes every write through one helper, so a bring-up
> correction is a one-line change. Getting it backwards reads as dead hardware, and this is the
> only light inside a closed case.

---

## 3. What changed in the tree

**One unit, two boards.** `units/sleep_button/screens.cpp` is untouched — it reaches hardware only
through `backlightSet`/`knobEvent`/`knobDown` and state only through `g_pending`/`settings*`/
`library::`, which is exactly the split `core/unit.h` exists to enforce. There is no new `units/`
directory.

### `src/boards/button_common/` — new

The two files both button boards need identically, moved out of `boards/esp32s3cam/`:

- **`button.{h,cpp}`** — the debounce + Short/Double/Triple/Long classifier. `buttonInit()` now
  takes the pin, which is the only thing the two boards disagree about. The thresholds are
  properties of the *switch* (a FILN FLM12-FJ-6 on both units), and `04` §1 already flags this
  state machine's timing as delicate — two copies would drift.
- **`config_server.{h,cpp}`** — the `:8080` page: sockets, routing and ~13 KB of embedded HTML,
  with zero board coupling. Two copies would mean every page change lands twice or not at all.

**Not in `core/`**, and that is deliberate: `+<core/>` sweeps into every env, so putting
`<WebServer.h>` there would drag it into `nest`, `sleep-machine` and `sonos-jukebox` — the mirror
image of the `art_cache.cpp` problem (issue #7) that `core/ui/` exists to prevent. The directory
defines no `boardInit()`/`uiInit()`, so it does not trip the one-board-per-env link guard. See
its README.

### `src/boards/xiao_esp32s3/` — new

`pins.h`, `board.cpp`, `bringup.{h,cpp}`. `board.cpp` is a near-copy of `esp32s3cam/board.cpp`:
same LEDC block, same low-side inversion in `backlightSet()`, same stubs for everything this
board does not have.

### The unit id — do not merge it back

`core/net/updater.cpp:unitId()` and `core/webconfig.cpp:registrationJson()` both gained a
`UNIT_BUTTON_V2 → "button2"` branch **above** the `HEADLESS` fallback.

> ⚠️ **This is the one change here that could ship a broken device.** Both button units are
> `HEADLESS`, and `updater.cpp` derives its pull-OTA manifest key from that macro alone. Without a
> distinct id, the XIAO unit asks the manifest for `"button"` and pull-flashes the ESP32-S3-CAM's
> binary. It is the same ISA, so it **boots happily** and then drives pins that do not exist on
> this board — a dead button that reports healthy on the dashboard. The jukebox already shipped
> the adjacent version of this bug (registered as `jukebox`, requested `unknown`).

The CI guard that cross-checks the two ladders **also had to be fixed**: its
`grep -oP 'return "\K[a-z]+'` is letters-only, so it did not fail on `button2` — it **truncated**
it to `button` on both sides, collapsed the two units into one entry, passed, and went silently
blind to exactly the pair it exists to keep apart. It is `[a-z0-9]+` now.

### `platformio.ini`

`[env:button-v2]`, `[env:button-v2-bringup]`, `[env:button-v2-ota]`. Beyond the board directory
the only deltas from `[env:sleep-button]` are `board`, the 8 MB flash/partition override (`[env]`
targets a 16 MB board), `-DUNIT_BUTTON_V2` and the hostname. `[env]`'s `qio_opi` memory type
already matches the XIAO's own board JSON.

### CI

`- { env: button-v2, unit: button2, release: true }`. `unit` must be `button2` so the published
`firmware-button2.bin` matches what `unitId()` asks the manifest for.

---

## 4. Phases

| # | phase | status |
|---|---|---|
| A | firmware: unit id, `button_common/`, board layer, envs, CI | ✅ build-verified, 5/5 envs green |
| B | hardware bring-up on a real XIAO | ⬜ needs the board |
| C | the case | ✅ both STLs watertight; not yet printed |

### Phase B, in order

`button-v2-bringup` links neither `core/` nor a unit, the same way `sleep-button-bringup` and the
es3c28p audio/mic tests isolate one subsystem.

```bash
P=$(ls /dev/ttyACM* | head -1)                    # the port BUMPS on every reset — see below
tools/pio run -e button-v2-bringup -t upload --upload-port "$P"
~/.platformio/penv/bin/python tools/readser.py "$P" 40   # system python has no pyserial here
```

> ⚠️ **Flashing this board is not like the others, and the first two failures look like dead
> hardware.** The XIAO has **no UART bridge chip** — it enumerates as the S3's built-in
> USB-Serial-JTAG (`303a:1001`, *not* the `2886:0056` in the board JSON). Two consequences, both
> already handled in `platformio.ini`, both of which cost a confusing failure first:
> - `[env]`'s **`upload_speed = 921600` cannot be negotiated** over a virtual serial port. esptool
>   connects, IDs the chip, uploads and runs the stub, then dies with `A fatal error occurred: No
>   serial data received.` These envs pin **115200**.
> - **esptool's stub re-initialises USB-Serial-JTAG mid-flash**, which drops the usbip attachment
>   under WSL (`vhci_hcd ... urb->status -104`) and fails as `Unable to verify flash chip
>   connection`. These envs flash from ROM with **`upload_flags = --no-stub`**.
>
> And the reset at the end of a flash **re-enumerates the device, bumping `/dev/ttyACM0` to
> `ttyACM1`** — the WSL gotcha CLAUDE.md warns about, which here fires on every single upload.
> Resolve the port dynamically; do not hardcode it. Kill any `readser.py` first, or it holds the
> old node and the upload fails with "the port doesn't exist".

1. **Memory/flash report** — expect PSRAM 8388608 and flash 8388608. Flash reading 16 MB means the
   env inherited `[env]`'s `flash_size` and the partition table is wrong.
2. **LED polarity** — it drives GPIO21 both ways, labelled. Set `PIN_STATUS_LED_ACTIVE_LOW` to
   match and stop thinking about it.
3. **Button on GPIO8** — one PRESS + one RELEASE per push, no repeats, and the boot-time read
   proves a press at power-on is harmless. The `held=` times are what
   `button_common/button.cpp`'s `DEBOUNCE_MS`/`LONG_PRESS_MS` are set from, and the tool to reach
   for if a double press ever registers as two singles.
4. **Ring on GPIO9** — three slow fades. If it never lights: white → 5V, black → D10, and you are
   on USB power. There is no pin sweep here (the cam-button needed one because its four wires go
   onto an 8-pin header that is easy to miscount); four soldered pads have one candidate.
5. Then flash `button-v2` and check `:2323` and `:8080`, and **RSSI with the antenna fitted and
   the lid on** — see §1.

---

## 5. Open

- **Phase B has not been run.** Everything above is build-verified only.
- ~~LED polarity~~ — **settled: active-LOW** (§2). No change was needed.
- **u.FL antenna RF in the closed box** (§1) — the one genuine regression of the swap.
- **The FLM12-FJ-6 is now measured** — 14.0 mm tip to tail, which contradicted both readings of
  the datasheet (`04` §7.1a is closed). The remaining unmeasured term is the **dome height**
  (`BUTTON_HEAD_T`, still the datasheet's 1.5), and it moves the case height 1:1.
- **`hardware/cam-button` has NOT been re-derived against that measurement** and is therefore
  ~3 mm taller than it needs to be. Left alone deliberately: its STLs are committed and the case
  may already be printed. See that unit's README §7.
- **The case has not been printed.** Test-coupon the Ø12 bore first.
