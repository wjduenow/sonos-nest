# 14 — button-v3: the sonos-button with a screen that explains it

**Board:** Waveshare ESP32-S3-LCD-1.47B (`src/boards/waveshare_s3_lcd147/`)
**Unit:** `units/sleep_button/` — **unchanged**, shared with sonos-button and button-v2
**Env:** `button-v3` · **Unit id:** `button3` · **Hostname:** `sonos-button3`
**Case:** `hardware/button-v3/` (not started)

The same product as [04](04-sonos-button-plan.md) and [11](11-button-v2.md) — press to start the
configured playlist, press again to stop, double and triple press have their own slots — on a board
that also carries a 1.47" ST7789. The screen is **dark by default**, wakes on a tap or a press, and
shows **one QR code** plus four status lines for 20 seconds.

It exists because a headless button cannot answer two questions:

1. **Before provisioning:** which SoftAP to join. `core/net/portal.cpp` raises an open
   `<hostname>-setup` network and waits, and there is no way for the device to tell you its name.
2. **After:** what its own address is, so the `:8080` page holding its three press slots is one
   camera-point away instead of a hunt through a router lease table.

> ## Status — 2026-09-08
> **WORKING ON HARDWARE. The whole feature is proven end to end: the screen wakes on a single tap
> and the QR scans. What is left is the harness (switch + ring) and the case.**
> - ✅ Software: `lib/qrcodegen/` vendored, `core/board.h` extended, all five existing boards
>   stubbed, `waveshare_s3_lcd147/` written, three envs, both unit-id ladders, CI matrix row.
> - ✅ Builds: `button-v3` 1,136,505 B flash / 53,308 B static RAM. `nest`, `sleep-machine`,
>   `sleep-button`, `button-v2`, `sonos-jukebox` all still build.
> - ✅ **Hardware, bring-up run 1:** 16 MB flash / 8189 KB PSRAM / 359 KB heap (env correct);
>   ST7789 draws 320x172 with **the 34-px column offset correct** and **colour order correct**
>   (no R-B swap, so `ips=true` is right); **QMI8658 ACKs at 0x6B, `WHO_AM_I = 0x05`**; GP2 idles
>   HIGH on its pull-up.
> - ✅ **Bug found and fixed by the bring-up:** it reported `press #1 — held 4210 ms` on a board
>   with nothing wired to GP2. The debounce state was seeded to "pressed" instead of read from the
>   pin, so the first loop saw a phantom release and printed uptime as hold time.
>   `button_common/button.cpp` seeds correctly; the bring-up copy did not.
> - ✅ **`TAP_JERK_LSB` measured, not guessed** (45 s capture): idle floor **44-95** with one
>   excursion to 471; deliberate taps **3674-80123**, median ~10k. Set to **1200**. The original
>   guess of 2250 was too HIGH — a lightly-tapped case could have missed.
> - ✅ The "bimodal noise" seen in run 1 was **the board being handled**, not ODR aliasing. A clean
>   capture has no second mode, so the poll needs no data-ready gating.
> - ✅ **App on hardware** (`v0.4.2-96-g535fbef`): Wi-Fi as `sonos-button3` @ 192.168.68.103, zone
>   discovery (8 zones, Master Bedroom), 15 playlists published, portal registration, log mirror on
>   :2323, `heap 233 KB free / 213 KB min`. `hasScreen=true`, so the brightness card renders.
> - ✅ **The screen works as designed.** Lights for 20 s at boot, goes dark, and **ONE TAP wakes
>   it** — the measured 1200 threshold is right on a bare board. **The QR scans** and opens
>   `http://<ip>:8080`. 29x29 modules at 4 px/module is comfortably readable by a phone.
> - ⬜ Ring + switch still unwired; WS2812 bead still unobserved.
> - ✅ **HARNESS SOLDERED AND PROVEN 2026-09-09.** Header pad 1 is live VBUS, the ring lights and
>   dims under PWM off GP4, and GP2 classified four consecutive presses (190/210/220/190 ms) with
>   no chatter. **The last item that could have forced a BOM change is closed** — the low-side ring
>   drive carries over from button-v2 completely unchanged.
> - ✅ **REPLACEMENT BOARD 2026-09-09**: panel good, QR on screen, harness working, and the printed
>   case's **mounting height confirmed** — the glass sits flush in the bezel.
> - ✅ **PROVISIONING QR WORKS 2026-09-10.** Held through power-on, the device raises its setup AP
>   and paints "Scan to set up Wi-Fi" with `http://192.168.4.1/`, lit indefinitely. That is the
>   feature the screen exists for, and it is the last of the four originally-specified behaviours
>   to be proven on hardware.

---

## 1. The board

Identified from the board itself: the ESP-IDF app descriptor at flash `0x10000` in its factory
firmware reads `project name = ESP32-S3-LCD-1.47B` (built Aug 13 2026, IDF v5.5.1). ESP32-S3R8,
8 MB PSRAM, **16 MB flash**, USB-Serial-JTAG (`303a:1001`), MAC `80:45:6b:35:d7:38`.

Same silicon as the nest and the XIAO, so it stays on the pinned `espressif32@6.9.0` and needs no
`tools/pio` change.

| Function | GPIO |
|---|---|
| LCD MOSI / SCLK / CS / DC / RST / BL | 45 / 40 / 42 / 41 / 39 / 46 |
| I2C SDA / SCL — QMI8658 @ `0x6B` | 48 / 47 |
| WS2812 bead | 38 |
| microSD CLK / CMD / D0 / D1 / D2 / D3 | 14 / 15 / 16 / 18 / 17 / 21 |
| Battery ADC · BOOT | 1 · 0 |
| **button** (harness) · **ring gate** (harness) | **2** · **4** |

### The wiring, and the risk that closed

The pin header is verified from Waveshare's interface drawing:

```
LEFT  rail, USB-C end first:  5V(VBUS)  GND  3V3(OUT)  GP0  GP2  GP3  GP4  GP5  GP6
RIGHT rail, USB-C end first:  TX  RX  VBAT  GND  GP11  GP10  GP9  GP8  GP7
```

**5 V is on the header**, which was the one open question that could have forced a BOM change: the
ring is white (Vf ≈ 3.1 V) and cannot be sourced from a 3.3 V pin, so both existing buttons drive
it low-side off a 5 V rail. That arrangement carries over unchanged.

All four wires land on the LEFT rail — 5V (pad 1), GND (pad 2), GP2 (pad 5, switch), GP4 (pad 7,
ring cathode). **GP3 sits between them and is deliberately skipped**: it is the S3's JTAG-select
strapping pin, the only strapping pin on this header. The gap in the harness is the reminder, the
same way `xiao_esp32s3/pins.h` skips D2.

### ⚠️ The panel is offset inside the controller, and getting it wrong is not a crash

The ST7789 has 240 columns of GRAM; this glass has 172, wired to columns 34..205. **Every address
window needs +34 on X.** Omit it and the picture still draws — shifted, with a band of garbage at
one edge — which reads as "bad panel" rather than "bad constant". Arduino_GFX 1.3.1 takes it as a
constructor argument, so there is nothing to patch:

```cpp
new Arduino_ST7789(bus, RST, /*rot=*/1, /*ips=*/true, 172, 320, /*col_offset1=*/34, 0, 0, 0);
```

The bring-up's test pattern is a 1-px border hard against all four edges, because that is the only
thing on this screen that reveals a wrong offset.

---

## 2. What changed in the tree

### `lib/qrcodegen/` — new

Nayuki's QR encoder, MIT, lifted from **LVGL's own vendored copy** (which the jukebox already runs
on hardware) and de-LVGL-ified: LVGL's include shims became plain `<stdbool.h>`/`<stddef.h>`/
`<stdint.h>`, and `LV_ASSERT` became a local no-op — which is what LVGL's default configuration
already compiles it to, so behaviour is byte-identical to what ships on the jukebox.

**Why not just link LVGL?** Its `LV_MEM_SIZE` pool is 96 KB out of this unit's ~243 KB of internal
heap, the tightest resource on the board, and CLAUDE.md already records pool exhaustion as a UI
*freeze*. For a QR and five text lines on a screen that lights twice a day, no. Full argument in
`lib/qrcodegen/VENDORING.md`.

### `core/board.h` — four additions, all stubbed on the other five boards

```c
void ringSet(uint8_t pct);        // the illuminated button, split out of backlightSet()
bool tapDetected();               // one-shot, sampled inside the call
bool infoScreenPresent();
void infoScreenShow(const char *qrText, const char *caption,
                    const char *const *lines, uint8_t nLines);
void infoScreenOff();
```

> ⚠️ **`backlightSet()` used to BE the ring.** On both screenless buttons the ring is the only light
> on the box, so mapping it onto the backlight HAL cost nothing. This board has *both*, so they had
> to split. `esp32s3cam` and `xiao_esp32s3` keep `backlightSet()` delegating to `ringSet()`, which
> is what makes `main.cpp`'s boot-time `backlightSet(settingsBrightness())` still mean the right
> thing there. **The unit must never call `backlightSet()` to light the screen** — on the other two
> boards that would move the ring. `infoScreenShow()` owns the backlight instead, reading
> `settingsBrightness()` itself, exactly as `uiSoundPlay()` reads `settingsUiSound()`.

The two levels come from settings that already differ correctly: `settingsRing()` floors at **0**
(a ring may be fully off), `settingsBrightness()` floors at **10** (nobody can blank an LCD and
lose the UI needed to un-blank it). No new NVS keys.

Rendering — including QR encoding — lives in the **board**, so the encoder and Arduino_GFX stay in
one env's `lib_deps` and never reach the other buttons through `+<core/>`. Policy lives in the
**unit**. Same split as the wake word.

### `src/boards/waveshare_s3_lcd147/` — new

`pins.h` · `board.cpp` · `display.{h,cpp}` (ST7789 + the page painter) · `imu.{h,cpp}` (QMI8658) ·
`bringup.{h,cpp}`.

`boardInit()` **returns true even if the panel fails**. On a screen unit a dead panel means a dead
product; this one is a button, and with no display it degrades to exactly a sonos-button — a
working device. Reporting failure would only print `board init FAILED` about a unit that is fine.

### `units/sleep_button/` — one unit, now three boards

Everything screen-related is inline under `#ifdef BUTTON_SCREEN`, so the two screenless envs
compile it away — the technique `core/sonos/gena.cpp` uses to become an empty TU. **A new *file*
here would have been swept into all three envs** by the `units/<unit>/` glob, and only one of them
links a graphics library. Same trap as `core/ui/`, one level down.

- `uiProvisioning(apSsid)` — already called by `appBoot()` with the exact SSID, already a no-op
  here — now paints `WIFI:T:nopass;S:<ssid>;;`, which phone cameras treat as "join this network".
  Left lit with **no timeout**: there is no uiTask yet to expire it, and a setup screen that goes
  dark while someone fetches their phone has failed at its only job. The first `uiTick()` clears it.
- SSID characters `\ ; , :` are escaped. Not theoretical — the SSID is `settingsDeviceName()`, free
  text from the config page, and an unescaped `;` truncates the payload into a QR that joins the
  wrong network.
- Room names are folded to 7-bit ASCII. The built-in GFX font has no glyphs above 0x7F and
  `setUTF8Print()` needs `U8G2_FONT_SUPPORT`, which this build does not enable, so "Küche" would
  otherwise render as garbage blocks.
- The page repaints only when its **content** changes (a signature compare at 1 Hz), because a
  repaint is ~40 ms of SPI and doing it every second would visibly flicker.

### Tap detection — software, not the CTRL8 engine

The QMI8658's own tap interrupt reports on an INT line **this board documents nowhere** — not the
wiki, not the vendor demo, which exposes neither CTRL8 nor CTRL9 beyond a `#define`. So `imu.cpp`
polls the accelerometer (±8 g @ 500 Hz) every 5 ms from `uiTick` and fires on summed per-axis jerk.
Gravity cancels in the difference, so it needs no baseline and no orientation assumption — the box
can be mounted any way up.

> ⚠️ **`TAP_JERK_LSB` = 1200, measured on a BARE BOARD — re-measure against the printed case.**
> 45 s capture, 2026-09-08: idle 44-95 LSB with one 471 excursion; deliberate fingertip taps
> 3674-80123, weakest 3674. That is ~8x of separation, so 1200 sits ~2.5x above the worst idle and
> ~3x below the weakest tap. In a case the PCB sits on ledges and the knock lands on a wall
> instead — if attenuation is 3x, the weakest tap arrives right at the threshold. The case is also
> where false positives live (a drawer, a glass set down), so both sides of the margin move.
> `TAP_DEBUG 1` in `imu.cpp` repeats the capture. Same role `WAKE_DEBUG` plays for the wake word.

### The unit id — do not merge it back

> ⚠️ **THREE units are now `HEADLESS`.** `updater.cpp:unitId()` derives its pull-OTA manifest key
> from that macro, so without `-DUNIT_BUTTON_V3` → `"button3"` sitting **above** the `HEADLESS`
> fallback, one button pull-flashes another's binary. Same ISA, so it **boots**, then drives pins
> that do not exist on that board: a dead device that reports healthy. Keep the branch, in the same
> order, in **both** `updater.cpp` and `webconfig.cpp`'s `registrationJson()`. CI cross-checks the
> two ladders; it cannot check that the flag is set in `platformio.ini`.

### `platformio.ini` and CI

`button-v3`, `button-v3-ota`, `button-v3-bringup`. Notes that matter:

- `default_16MB.csv` — **unlike both other buttons**, which are 8 MB parts.
- `upload_speed = 115200` + `upload_flags = --no-stub`, same as button-v2: USB-Serial-JTAG, not a
  UART bridge. Confirmed against this physical board.
- `lib_deps` is ArduinoJson + Arduino_GFX **only** — deliberately not `${env.lib_deps}`, which
  would drag LVGL and TJpg into a headless button.
- CI matrix gains `{ env: button-v3, unit: button3, release: true }`.

---

## 3. Phases

| # | phase | status |
|---|---|---|
| A | Software: board, unit, envs, ladders, CI | ✅ build-verified |
| B | Phase-0 bring-up on hardware | ⬜ |
| C | App on hardware: QR scans, screen sleeps, tap wake | ✅ 2026-09-08 |
| D | Provisioning QR raises the setup AP | ✅ 2026-09-10 |
| E | Case, `hardware/button-v3/` | ⬜ |

### Phase B, in order

```bash
export PATH="$PATH:$HOME/.platformio/penv/bin"
tools/pio run -e button-v3-bringup
P=$(ls /dev/ttyACM* | head -1)
tools/pio run -e button-v3-bringup -t upload --upload-port $P
python3 tools/readser.py $(ls /dev/ttyACM* | head -1) 60
```

It answers, in order: build config (16 MB flash / 8 MB PSRAM — anything else means the env is
wrong), panel + **the 34-px offset** (a 1-px border must touch all four edges), QMI8658 address via
ACK **and** `WHO_AM_I`, WS2812 bead, ring fade, then a live loop printing press timings and
accelerometer jerk peaks.

> ⚠️ **Flashing gotchas, same family as button-v2.** No UART bridge, so 921600 cannot be negotiated
> and esptool's stub re-inits USB mid-flash — hence `115200` + `--no-stub`. The post-flash reset
> **re-enumerates `/dev/ttyACM0` → `ttyACM1` every time**; resolve the port dynamically and kill any
> `readser.py` first. Under WSL, `usbipd attach` drops on every re-enumeration — use `--auto-attach`.

> ⚠️ **Build the jukebox LAST if you intend to flash it** — building an S3 env deletes
> `.pio/build/sonos-jukebox/`. And **commit before building what you flash**, or `FW_VERSION` reads
> `<tag>-dirty` and the coredump workflow cannot tie a dump to a commit.

---

## 3b. ⚠️ Handling — one board already lost

The LCD is bonded across the entire PCB face, so **soldering the harness cracked one panel**
(2026-09-09: passed the bring-up in the morning, cracked by the afternoon). There is no
unsupported FR4 on a 36 x 20 mm board with glass over all of it — bending it while soldering the
castellated pads goes into the glass. Support it face-down, flat and soft; hold the wire, not the
board.

**A cracked panel is a whole-board replacement**, because the two are bonded.

> ⚠️ **Firmware cannot detect this.** `displayInit()` only verifies its objects allocated —
> Arduino_GFX 1.3.1's `begin()` returns void, so there is no return code to check and no register
> read-back in that driver. A cracked panel reports a clean boot with no error and simply shows
> nothing, which is indistinguishable from the app's normal 20 s screen-off behaviour. The
> bring-up's PERSISTENT test pattern is the only reliable way to tell the two apart.

## 4. Open

- ~~The harness: switch on GP2, ring on GP4~~ — **SOLDERED AND PROVEN 2026-09-09.** Pad 1 is live
  VBUS; the ring dims under PWM; four presses classified cleanly with no chatter. Worth noting for
  the tap threshold: pressing the button produces accelerometer jerk up to **6597**, about 5.5x
  `TAP_JERK_LSB`, so a press always registers as a tap too. Harmless — both wake the screen — but
  it means the two inputs can never be told apart by the IMU.
- ~~The provisioning QR has never run~~ — **PROVEN 2026-09-10** by holding the button through
  power-on.
  > ⚠️ **The SSID ESCAPING is still untested.** `sonos-button3-setup` contains none of the `\ ; , :`
  > characters `wifiQrEscape()` exists to handle, so that function was a no-op in the test that
  > passed. The escaping only matters once an owner sets a device name containing one — and a bad
  > payload does not look wrong, it just sends the phone to a network that does not exist. Set a
  > device name with a semicolon on the `:8080` page and repeat to cover it.
- ⚠️ **There is no way to re-provision without physical access.** The only trigger is a hold
  through power-on, which is fine on a desk and useless on a wall — exactly the situation this
  screen exists to solve. A config-page field that sets a flag and reboots into the portal would
  fix it, and would have turned a fiddly two-handed test into one HTTP request.
- ⚠️ **`TAP_JERK_LSB`** — a guess. Measure it against the printed case, not a bare board: a case
  transmits a knock quite differently from a PCB on a desk.
- ⚠️ **Mounting-hole centres.** The board is **36.37 × 20.32 mm** with four **M2** corner holes
  (both from Waveshare's dimension drawing). The hole *centres* are deliberately not recorded: the
  drawing carries 1.97 / 2.40 / 2.00 / 3.52 / 17.78 without unambiguous leaders, and 17.78 is
  exactly 7 × 2.54, far more likely to be the rail-to-rail spacing than a hole pitch. **Measure
  them.** This project has already paid once for trusting a datasheet over calipers (the FLM12-FJ-6
  depth). M2 corner holes are a real gain over `button-v2`, whose XIAO has none — the lid there is
  structural because of it.
- ⚠️ **Screen orientation is a case decision.** `DISPLAY_ROTATION 1` (landscape, 320×172) because
  the active area is ~32.4 × 17.4 mm and its long axis has to lie along the box's long axis to fit
  a side wall at all. One edit in `pins.h` plus a re-derived cutout if the case says otherwise.
- **The QMI8658 INT line** is undocumented; the hardware tap engine is the upgrade path if the
  software detector proves jumpy on a real case. Needs the schematic (`poppler-utils` is not
  installed here). The measured 8x margin makes this look unnecessary for now.
- ~~The 250-300 resting mode might be ODR/poll aliasing~~ — **disproven** 2026-09-08. A genuinely
  untouched board has a clean 44-95 floor and no second mode; run 1 was a board being handled.
- **microSD is not mounted.** The pins are recorded so nobody reuses them; `localStorageRoot()`
  returns nullptr. Nothing on this unit wants storage yet.
- **The WS2812 bead is unused by the app.** It is inside the case once assembled, so it is a
  bring-up liveness tell only — the same role the XIAO's GPIO21 LED plays.
