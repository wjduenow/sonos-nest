# jukebox-7 — sonos-jukebox wall case

3D-printed flush wall enclosure for the **sonos-jukebox** unit: an ELECROW CrowPanel Advance 7"
ESP32-P4 HMI board (1024×600 MIPI-DSI) with a physical control column beside the screen.

- **Board mechanical spec:** [`crowpanel-p4-7-physical-spec.md`](crowpanel-p4-7-physical-spec.md) —
  outline, mounting holes, connector map, power tree, measured thickness. **Read it first**, and
  note that the board is **mirrored** relative to the vendor's Eagle files.
- **Firmware plan:** `plans/07-sonos-jukebox.md`
- **Industrial design:** the `/sonos-jukebox-design` skill (`tokens/hardware.css` is the dimension
  source of truth; `industrial/` holds the elevations).

Parts are generated with the repo's Python CSG toolchain (trimesh + manifold3d) — see
[`../README.md`](../README.md). Per repo convention, `hardware/` commits stay separate from firmware.

## Design intent

A landscape touchscreen sitting **flush to the wall**, with a push-to-select rotary dial over a 2×2
grid of momentary caps in a column to the **right** of the screen. Matte white. Power enters through
the **back** from a cable in the wall.

| | |
|---|---|
| Face | **240 × 136 mm** (see [`wall/`](wall/) for why each dimension is what it is) |
| Depth | **23.5 mm** — wrap-around bezel; depth set by the plugged J10 cable |
| Corner radius | 14 mm (design token) |
| Screen cutout | **165.5 × 100.6** — cut to the module outline, not the lit area |
| Mounting | Keyhole slots in the rear shell — lowest profile, adds 0 mm |
| Printer | Bambu P2S, ~254 × 254 mm bed — the face prints flat in one piece |

## Bill of materials

### Main board
**ELECROW CrowPanel Advance 7" ESP32-P4 HMI AI Display**, SKU `DHE04107D`, **PCB rev V1.0**.
176.90 × 104.00 × 17.5 mm envelope, 4× M3.2 on a 170.90 × 98.00 pattern.
Full detail in [`crowpanel-p4-7-physical-spec.md`](crowpanel-p4-7-physical-spec.md).

### USB-C breakout — rear power entry
SparkFun-pattern **"USB C Breakout"** (silkscreen rev `V10`), red PCB, 6 castellated/through pads:
`VBUS · GND · CC1 · D− · D+ · CC2`.

| dimension | value | how known |
|---|---|---|
| Height ("tall" axis) | **21.4 mm** | measured |
| Depth — receptacle front face → rear board edge | **14.5 mm** | measured |
| Overall thickness (PCB + receptacle) | **4.75 mm** | measured |
| Mounting holes | **2**, one either side of the receptacle | — |
| Hole spacing, **inside edge to inside edge** | **13.3 mm** | measured |
| Hole diameter | **≈ 3.5 mm** | **VERIFY** — scaled from the product photo |
| Hole spacing, **centre to centre** | **≈ 16.85 mm** | **VERIFY** — derived (13.3 + Ø3.5), and independently scaled from the photo at ~16.9 mm |
| Hole centre offset from the receptacle front face | **≈ 4.2 mm** | **VERIFY** — scaled from the photo |

> ✅ **CC pull-downs are fitted.** Two 0603 resistors marked `512` (= 5.1 kΩ) sit on the CC1 and CC2
> traces. This is the part that makes a USB-C **PD charger** actually deliver 5 V; breakouts without
> them power nothing. Do not substitute a cheaper board without checking for these.

**Only `VBUS` and `GND` are used** — two wires to the main board's `J10` pin 3 (`+5V_IN`) and pin 4
(`GND`). No USB-C plug goes inside the case. See the power tree in the board spec.

**J10 is a JST XH 2.54 mm 4-pin**, so the board end needs no soldering — a pre-made XH pigtail
plugs straight in. Elecrow silkscreens `RX3 / TX3 / +5V / GND` beside it on the **back** of the
board; the pins run top to bottom 1→4, so **`+5V` is 3rd from the top and `GND` is the bottom
pin**. Physically it is on the same edge as the two USB-C ports, below both of them, ~20 mm up
from that corner and ~17 mm above the corner mounting hole.

⚠️ **Do not use `J3` for power.** It is a 2-pin JST PH that looks ideal, but the net is `VBAT` — a
LiPo battery input feeding the charger. `J4`/`J6` are the speakers.

Its opening faces the **board's edge**, which is why the case carries a 10 mm gap on the left
(`CLR_LEFT`) — the housing plugs in horizontally and sticks out past the board.

#### Power only, by decision — updates go over OTA

The rear port carries **5 V and GND, nothing else**. Two paths to a data port were considered and
rejected:

- **Breakout data pads → J10's serial pins.** Does not work. USB `D+`/`D−` is a differential USB
  bus, not UART; it needs a bridge chip. And J10's pins are **UART3**, not the console UART — the
  firmware logs on UART0 and the ROM bootloader listens there, so even with a bridge you would get
  an app-level debug stream you had to write yourself, and never flashing.
- **Rear port → `J1` with a passive USB-C female-to-male extension.** This *does* work: J1 goes
  through 0 Ω links to the **CH340K** bridge, whose `DTR#`/`RTS#` drive the auto-reset circuit, so
  it gives console *and* flashing with no button presses. (`J16` is different — straight to the
  P4's native USB `DP`/`DM`.) Rejected as unnecessary complexity for a unit that is close to done.

**So: OTA is the update path.** That is sound on *this* board specifically — the plan notes the
internal-SRAM pressure that makes nest OTA unreliable is "genuinely gone" here, with ~70–100 KB of
internal heap free at idle against the S3's ~150 KB total.

**Recovery, if OTA ever cannot reach it:** lift the unit off its keyholes, open it, and plug USB
into **J1** — the CH340K port. The keyhole mount exists partly to make that a two-minute job.

> One caveat worth knowing: the ESP-Hosted link fault in `plans/07-sonos-jukebox.md` is still open,
> and an OTA attempted while the link is down will fail. That is harmless — a failed transfer leaves
> the running firmware untouched — but retry rather than assume the image is bad.

> Note the routing consequence: `J10` is at the **far left** of the front view (front-x ≈ 6.2,
> y ≈ 19.8) while the control column is on the right, so the power pair crosses the width behind the
> PCB. Two low-current wires, so this is a channel-routing problem, not an electrical one.

### Rotary encoder — **Arduino Modulino® Knob**, SKU `ABX00107`
Datasheet mirrored here: [`modulino-knob-ABX00107-datasheet.pdf`](modulino-knob-ABX00107-datasheet.pdf).

| | value |
|---|---|
| Board | **41.0 × 25.36 mm**, 1.6 ±0.2 thick |
| Mounting | **4× Ø3.2**, spacing **32.0 horizontal × 16.0 vertical** |
| Interface | Qwiic / I²C, **3.3 V**, ~3.4 mA |
| **I²C address** | **0x3A as Arduino's `Wire` addresses it.** The datasheet's `0x76`/`0x74` are **8-bit**; `Wire` is 7-bit, so the bus scan finds it at `0x74 >> 1 = 0x3A` (its pinstrap byte reads back as `0x74`, confirming it). Software configurable. |
| MCU | STM32C011F4 |
| Encoder | **Bourns PEC11J-9215F-S0015** — 15 PPR, **30 detents**, momentary push switch |
| Shaft | **Ø6.0**, D-flat over the last **5 mm** (4.5 across the flat), **L1 = 15.0** from the bushing flange, **LB = 7.0** bushing (Ø7.0) |

> ✅ **Verified on hardware:** the dial drives volume and play/pause. A bus scan with it attached
> reads **0x2F, 0x3A, 0x5D** — it clears the GT911 at 0x5D and the unidentified device at 0x2F.
> ⚠️ Expect the same 8-bit/7-bit shift on the **PCF8574**: its advertised 0x20-0x27 are 7-bit
> already, but confirm with a scan rather than trusting the number.
> And the Modulino ships with **no I²C pull-ups fitted** — which is what we want, since the
> CrowPanel's bus already has them. Don't add the optional 4.7 k 0402s.

> ⚠️ **The board cannot sit on the case floor.** Shaft tip to Modulino PCB is ~21.5 mm
> (`KNOB_BODY_H + L1`), so a floor-mounted board would leave the shaft only ~2 mm proud of a face
> whose cap is 14 mm deep. It rides on **standoffs 6 mm above the floor** — see `KNOB_TIP_Z` in
> `wall/case_params.py`, which positions the board from the shaft tip rather than from the floor.

> Note the design system's control-layout spec says "24-detent enc." — the real part is **30
> detents**. Cosmetic, but it is what the firmware will see per revolution.

### Button I/O expander — **Adafruit PCF8574 breakout**, product `5545`
STEMMA QT / Qwiic. Outline and holes are **exact**, taken from Adafruit's own Eagle file
(`Adafruit PCF8574 QT.brd`, [github.com/adafruit/Adafruit-PCF8574-PCB](https://github.com/adafruit/Adafruit-PCF8574-PCB)) rather than the product page.

| | value |
|---|---|
| Board | **25.40 × 17.78 mm** (exactly 1.0" × 0.7"), **4.6 mm** tall incl. connectors |
| Mounting | **2× Ø2.5 plated**, at (2.54, 2.54) and (22.86, 2.54) → **20.32 mm centre-to-centre** |
| GPIO | **8** (four are used) |
| **I²C address** | **0x20**, jumpers A0/A1/A2 give **0x20–0x27** |

> ✅ 0x20–0x27 clears the GT911 (0x5D), the unidentified 0x2F and the Knob (0x76).
> All four bus addresses are now known and distinct.

> Wiring note: PCF8574 inputs idle high on a weak (~100 µA) internal source, so the buttons
> simply switch to **GND** — no external pull-ups. The chip also has an **INT** output if
> polling ever proves too costly; poll it from **netTask, never the UI task**, since the bus is
> shared with the touch controller.

It mounts **flat on the case floor beneath the button grid** — the switch bodies hang down from
the face plate at z ≈ 19.5 while the expander lives at z 4.0–8.6, so they share the same XY area
but never collide in Z. That was the only way to fit it: stacking it in the column alongside the
dial, buttons and USB-C breakout needs ~112 mm of a 122 mm interior, which leaves no usable gaps.

### Bus cable — **Adafruit `4528`**, Grove → STEMMA QT / Qwiic / JST-SH, 100 mm
<https://www.adafruit.com/product/4528>

**The two ends are different connectors, and this is easy to miss.** `J13` on the CrowPanel is
**Crowtail — 4-pin, 2.0 mm pitch** (Eagle package `CONNECTOR_4P-SMD-2.0`), which is the Grove
standard. Both control boards are **Qwiic / STEMMA QT — 4-pin JST-SH, 1.0 mm**. Nothing plugs into
anything else without this adapter.

| | value |
|---|---|
| Ends | Grove 4P 2.0 mm ↔ JST-SH 4P 1.0 mm |
| Length | **100 mm** — ample; J13 is at front-x ≈ 137, the column starts at ≈ 180 |
| Wires | **black GND · red V+ · blue SDA · yellow SCL** |

> ⚠️ **Do not splice your own.** The two standards run their pins in **opposite order** — Crowtail
> is SCL/SDA/3V3/GND, Qwiic is GND/3V3/SDA/SCL — so a straight-through 4-wire cable lands 3V3 on
> SDA and GND on SCL. The Adafruit cable maps by wire colour and gets this right; a hand-made one
> only does if you deliberately reverse it. Worth a continuity check either way.

One cable is enough for both boards: it reaches `J13` → **Knob**, and the second Qwiic socket on
the Knob daisy-chains to the **PCF8574** with an ordinary Qwiic–Qwiic cable.

### Still pending selection
- **4× momentary switches** — bodies TBD; the design calls for Ø13 caps on a 9 mm pitch.
- **1× dial cap** — Ø36, 14 mm proud, knurled, **Ø6 D-bore**.
- **1× Qwiic–Qwiic cable** for the Knob → PCF8574 hop (any length ≥ 50 mm).

Both I2C boards **daisy-chain into `J13`**, the board's Crowtail I2C connector (front-x ≈ 136.9,
y ≈ 96.57 — conveniently on the column side).

> ⚠️ **Check the GPIO breakout's address before buying.** The bus carries the **GT911 at 0x5D**, an
> **unidentified device at 0x2F** (see `plans/07-sonos-jukebox.md`) and now the **Knob at 0x76**.
> A collision would be silent and painful to diagnose.

## Parts

- **[`wall/`](wall/)** — the flush wall case: `shell.stl` (body + keyhole mount + breakout cradle)
  and `face.stl` (screen opening + control column). First parametric pass is built.

## Status

Geometry generates and is watertight. The **face plate is provisional**: the lit area's position on
the PCB is still a centred guess, and it is the only thing that moves the screen opening. Remaining
items are listed at the end of [`crowpanel-p4-7-physical-spec.md`](crowpanel-p4-7-physical-spec.md)
and in [`wall/README.md`](wall/README.md).
