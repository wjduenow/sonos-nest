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
| Face | **230 × 124 mm** (12.5 mm top band carries the keyholes; see [`wall/`](wall/)) |
| Depth | **22.0 mm** — exactly the design system's `--u7-depth` token |
| Corner radius | 14 mm (design token) |
| Screen cutout | 155 × 87 mm active area — **exact position pending measurement** |
| Mounting | Keyhole slots in the rear shell — lowest profile, adds 0 mm |
| Printer | Bambu P2S, ~254 × 254 mm bed — the face prints flat in one piece |

## Bill of materials

### Main board
**ELECROW CrowPanel Advance 7" ESP32-P4 HMI AI Display**, SKU `DHE04107D`, **PCB rev V1.0**.
176.90 × 104.00 × 16.5 mm envelope, 4× M3.2 on a 170.90 × 98.00 pattern.
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

> Note the routing consequence: `J10` is at the **far left** of the front view (front-x ≈ 6.2,
> y ≈ 19.8) while the control column is on the right, so the power pair crosses the width behind the
> PCB. Two low-current wires, so this is a channel-routing problem, not an electrical one.

### Control boards — **pending selection**
- **I2C rotary encoder board** (push-to-select) — outline, holes, shaft spec and **I2C address** TBD.
- **I2C GPIO breakout** with solder pads for the four button wires — outline, holes, **address** TBD.
- **4× momentary switches** — bodies TBD; the design calls for Ø13 caps on a 9 mm pitch.
- **1× dial cap** — the design calls for Ø36, 14 mm proud, knurled.

Both I2C boards **daisy-chain into `J13`**, the board's Crowtail I2C connector (front-x ≈ 136.9,
y ≈ 96.57 — conveniently on the column side).

> ⚠️ **Check addresses before buying.** The bus already carries the **GT911 touch controller at
> 0x5D** and an **unidentified device at 0x2F** (see `plans/07-sonos-jukebox.md`). A collision would
> be silent and painful to diagnose.

## Parts

- **[`wall/`](wall/)** — the flush wall case: `shell.stl` (body + keyhole mount + breakout cradle)
  and `face.stl` (screen opening + control column). First parametric pass is built.

## Status

Geometry generates and is watertight. The **face plate is provisional**: the lit area's position on
the PCB is still a centred guess, and it is the only thing that moves the screen opening. Remaining
items are listed at the end of [`crowpanel-p4-7-physical-spec.md`](crowpanel-p4-7-physical-spec.md)
and in [`wall/README.md`](wall/README.md).
