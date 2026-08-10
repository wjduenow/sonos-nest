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

### Button I/O expander — **dropped, not pending**
The four transport buttons and the **Adafruit PCF8574** expander that would have read them were
removed once the dial became the only control. Nothing needs buying, and the consequences are
worth stating because they simplified the whole build:

- The I²C chain is **one board** — `J13` → Modulino Knob. No daisy-chain hop, no second cable.
- The bus carries **GT911 0x5D · unidentified 0x2F · Knob 0x3A**, and nothing else.
- **The only soldering left in the entire build is the two USB-C power wires.** No carrier PCB,
  no switch wiring, no crimping.

If buttons ever come back, the analysis is in git history: PCF8574 at 0x20–0x27, 8 GPIO, mounted
flat on the floor beneath the grid (switch bodies hang from the face at z ≈ 19.5, expander at
z 4.0–8.6 — same XY, different Z), and 12 × 12 mm PCB-mount tact switches on a carrier at
`BTN_PITCH` 22.86 so they land on 2.54 mm perfboard. Panel-mount buttons do **not** fit: they are
20–25 mm long behind the face and only 12.4 mm is available.

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

**One cable is all it takes**, now that the expander is gone: `J13` → **Knob**, and the Knob's
second Qwiic socket stays empty.

### Magnets — **two sizes, and they are not interchangeable**

| | |
|---|---|
| Shell | **6 × Ø8.0 × 2.0 mm** discs |
| Face plate | **6 × Ø6.0 × 2.0 mm** discs |
| Total | **12** — neodymium, 2 mm thick throughout |

**Why the shell gets the bigger one.** Its bore has to be **Ø8.6 regardless**, because that bore
doubles as the socket receiving the face plate's registration spigot. Putting a Ø6 disc at the
bottom of it left a *stepped* hole — you had to drop the magnet 4 mm down a wide bore and hope it
found a small pocket, which is miserable to glue. An **Ø8 disc fills that same bore**, so the shell
side is one straight hole: drop it in, it lands flat, glue it.

The face plate keeps **Ø6** because its spigot is only Ø7.8 and cannot wall anything larger —
there is 0.75 mm of plastic around the disc as it stands.

> ⚠️ **Polarity.** Glue **all** shell magnets one way up and **all** face magnets the other, so
> every pair attracts. Mark one pole with a marker *before* any glue is opened — this is not
> recoverable afterwards. Same rule as `hardware/round-nest-2.8/wall/`.

The two faces meet across a deliberate **0.5 mm air gap** (`MAG_AIRGAP`): the plate seats on the
shell rim and the six blocks, and the magnets pull it down rather than defining where it sits.

### Still pending selection
- **1× dial cap** — Ø42, 14 mm proud, **Ø6 D-bore**, if you buy rather than print
  (`wall/knob_cap.stl` is the printed one). Must be a **D-shaft** knob, not knurled/splined.

## Assembly notes

### Wiring — what is crimped, what is soldered, what is neither

There are exactly **two** electrical connections to make, and only one of them needs an iron.

| link | connector pair | what it takes |
|---|---|---|
| USB-C breakout → `J10` (power) | JST **XH 2.54 mm** 4-pin | pre-made XH pigtail — **no crimping**. Solder **two wires to the breakout** |
| Modulino Knob → `J13` (I²C) | Crowtail 2.0 mm ↔ Qwiic JST-SH 1.0 mm | Adafruit `4528` adapter cable — **no soldering, no crimping** |

#### Power — two of the four wires, and only the breakout gets soldered

`J10` is a JST XH 2.54 mm 4-pin: `1 = UART_IN_TXD3 · 2 = UART_IN_RXD3 · 3 = +5V_IN · 4 = GND`.
Elecrow silkscreens `RX3 / TX3 / +5V / GND` beside it on the **back** of the board.

- **Board end: nothing to do.** A pre-made XH pigtail plugs straight in. No crimp tool, no iron.
- **Only pins 3 and 4 are used.** Leave the UART pair unconnected.
- **Breakout end: soldered.** `VBUS` → J10 pin 3, `GND` → J10 pin 4, onto the breakout's
  through-hole pads. ✅ Verified on the unit — it powers the device.

> ⚠️ **The breakout must have 5.1 kΩ CC pull-downs** (two 0603s marked `512` on CC1/CC2). Without
> them a USB-C **PD charger negotiates nothing and delivers no power at all**. The SparkFun-pattern
> board specified above has them; a cheaper substitute may not.

#### I²C — the two ends are different connector families

Covered in the bus-cable section above, and the one thing to carry away: **the pin orders are
reversed**, so a hand-spliced 4-wire cable puts 3.3 V on SCL. Buy the adapter.

#### Data over USB-C — deliberately not wired

The rear port carries **5 V and GND only**. Updates go over **OTA**, and the unit is readable over
the **TCP log mirror on :2323** (`core/net/logmirror.h`), which is why no data path was needed.
Three routes were investigated and rejected — recorded so they are not re-investigated:

| route | verdict |
|---|---|
| Test pads **P2 / P3** (`USB1_D+` / `USB1_D−`, beside J1) | **Electrically correct** — J1 goes through 0 Ω links to the CH340K, giving console *and* flashing with auto-reset. Rejected only because the pads are 1.5 mm and awkward to hand-solder. **This is the route to take if data is ever wanted.** |
| **`J2` / UART1** | Wrong UART. Console and the ROM bootloader are on **UART0** (GPIO37/38 → CH340K → J1). And USB `D+`/`D−` cannot join a UART at all without a bridge chip |
| The **Modulino** | I²C only; its SWD/UART header belongs to its own STM32 |

If it is ever wanted, the no-board-soldering version is a **USB-C male pigtail into `J1`**, joined
on the *breakout* side. It must be a **right-angle** plug — a straight one is ~20 mm and the
`CLR_LEFT` gap is 10 mm.

#### Magnet polarity

**6× Ø8 × 2 in the shell, 6× Ø6 × 2 in the face.** Glue **all** shell magnets one way up and
**all** face magnets the other, so every pair attracts — mark one pole with a marker before any
glue goes near them. Same rule as `hardware/round-nest-2.8/wall/`.

#### Routing inside the case

- `J10`'s connector opens toward the board's **left** edge, so its cable and housing live in the
  10 mm `CLR_LEFT` gap. That gap exists for this and nothing else.
- The power pair then crosses the width in a **recessed floor channel**, so it is not pinched
  under the PCB.
- **Local floor reliefs** at `J10` and `J13` give each plugged cable 2.0 mm of clearance. Without
  them there is only 0.5 mm behind those connectors.

### ⚠️ Mask the indicator LEDs before closing the case

Two LEDs sit inside the sealed cavity, and their light escapes — bouncing around the white
interior and out through the **0.3 mm clearance gap around the display module** (`SCREEN_CLR`)
and, faintly, through the face plate itself. It reads as a glow around the screen edge and is
easily mistaken for backlight bleed or under-infilled plastic. It is neither.

| LED | where | can it be switched off? |
|---|---|---|
| **D14** on the CrowPanel — red power indicator | back of the board, **2.38 mm from the edge, between the two USB-C ports**. In case coords front-view **x ≈ 15.4, y ≈ 66.8** — in the left channel where the J10 power cable runs | **No.** Hard-wired `VDD5V → R26 (5.1 kΩ) → D14 → GND`. No GPIO in the path |
| Power LED on the **Modulino Knob** | in the control column, directly behind the face | No |

**Fix: cover both with black electrical tape** at assembly. Do **not** use Kapton — it is
translucent amber and merely tints the leak. Removing `R26` would kill D14 properly, but it is an
0603 and not worth the risk.

This is an assembly step, not a print setting. Reprinting the face at 100 % infill does not fix
it, because the dominant path is the module clearance gap, not translucency.

## Parts

- **[`wall/`](wall/)** — the flush wall case: `shell.stl` (body + keyhole mount + breakout cradle)
  and `face.stl` (screen opening + control column). First parametric pass is built.

## Status

Geometry generates and is watertight. The **face plate is provisional**: the lit area's position on
the PCB is still a centred guess, and it is the only thing that moves the screen opening. Remaining
items are listed at the end of [`crowpanel-p4-7-physical-spec.md`](crowpanel-p4-7-physical-spec.md)
and in [`wall/README.md`](wall/README.md).
