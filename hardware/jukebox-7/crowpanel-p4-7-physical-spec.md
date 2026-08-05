# CrowPanel Advance 7" ESP32-P4 (DHE04107D) — physical spec for case design

Mechanical source of truth for the **sonos-jukebox** case (`hardware/jukebox-7/`).
Firmware plan: `plans/07-sonos-jukebox.md`. Pin map: `src/boards/crowpanel_p4_7in/pins.h`.

**Source: Elecrow's own Eagle files, PCB revision V1.0** —
`Eagle_SCH&PCB/1.0/ESP32-P4 Display 7.0 inch V1.0.brd` / `.sch` from
<https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen>

Everything below is **extracted from the board file**, not estimated from photos, unless the row
says **VERIFY**. Per `hardware/README.md`, VERIFY items must be caliper-checked before a final print.

> ⚠️ **Revision matters.** This is **V1.0** (printed on the top silkscreen). V1.1/V1.2 exist and at
> minimum reverse the SDIO data lines. Re-extract from the matching Eagle directory if the board in
> hand is not V1.0.

## Coordinate system

Eagle origin = **bottom-left corner of the PCB**, X right, Y up, viewed from the **top (component)
side**. All values in mm.

**VERIFY — which physical face is Eagle's "top".** Both the LCD outline (`U$113`) and every edge
connector are placed on the Eagle top layer, so the display and the connectors share a face and the
connectors are reached from the board's edges. Confirm before trusting any left/right value: with
the screen facing you and the two white 4-pin Crowtail connectors along the **upper** long edge, the
two USB-C ports should be on your **right**. If they are on your left, the front view is mirrored in
X and every X coordinate here must be read as `176.90 − x`.

## Outline

| | |
|---|---|
| PCB outline | **176.90 × 104.00 mm** (vendor docs say "180 × 105" — that is nominal, not the drawing) |
| Corner radius | **3.0 mm**, all four |
| Active display area | **155 × 87 mm** (vendor docs; position on the PCB **VERIFY**) |
| PCB thickness | **VERIFY** (assume 1.6 until measured) |

## Mounting holes — 4× M3.2 through

| ref | X | Y |
|---|---|---|
| J18 | 3.00 | 3.00 |
| J12 | 3.00 | 101.00 |
| J14 | 173.90 | 3.00 |
| J15 | 173.90 | 101.00 |

**Bolt pattern 170.90 × 98.00 mm**, 3.0 mm in from each corner.
Package `THO-M3.2*5MM-HOLE` — 3.2 mm drill, 5 mm pad.

## Connector map, by edge

### RIGHT edge (x ≈ 170–177) — the control-column side
| ref | package | Y | what |
|---|---|---|---|
| J10 | XH2.54-4P-SMD | 19.80 | UART3 TX/RX + **+5V\_IN** + GND — **the power input this case uses** |
| J16 | USB-3.0TYPEC-SMD | 41.57 | USB-C #2 (`+VBUS2`) |
| J1 | USB-3.0TYPEC-SMD | 62.47 | USB-C #1 (`+VBUS1`) |
| SW1 | SWITCH_6PIN_2P | 79.45 | 2-position slide switch — **unused; enclosed, no access** |

> SW1's function was never identified (no nets resolved for it in the schematic pass). It is being
> covered deliberately. **Set it to the working position and confirm the unit boots before closing
> the case** — if it turns out to gate power or boot mode, it is unreachable afterwards.

### TOP edge (y ≈ 91–97)
| ref | package | X | what |
|---|---|---|---|
| J2 | CONNECTOR_4P-SMD-2.0 | 18.00 | Crowtail **UART** (RXD1/TXD1/3V3/GND) |
| J13 | CONNECTOR_4P-SMD-2.0 | 40.00 | Crowtail **I2C** (SCL/SDA/3V3/GND) — **control-board daisy chain** |
| FPC3 | FPC 24P 0.5 mm | 88.50 | — |
| FPC2 | FPC 6P 0.5 mm | 126.05 | — |
| J9 | 7-pin 2.54 | 145.17 | radio module (SPI2, VDD5V_W) |
| J11 | 7-pin 2.54 | 160.67 | radio module (I2C1, IO9/IO10) |

### BOTTOM edge (y ≈ 3–7)
| ref | package | X | what |
|---|---|---|---|
| J4 | PH2.0WT-2PIN | 35.72 | speaker / power |
| J7 | 2X12-SMD1 | 88.43 | **24-pin GPIO header** (P4_IO3/4/5/25/27/28/49/50/51/52, VDD5V, 3V3, GND) |
| J3 | PH2.0WT-2PIN | 144.93 | speaker / power |
| J6 | PH2.0WT-2PIN | 160.93 | speaker / power |

### LEFT edge (x ≈ 3–5)
| ref | package | Y | what |
|---|---|---|---|
| K4 | KEY-4.5X4.5-SMD | 17.25 | **RESET** (the lower of the two) |
| K3 | KEY-4.5X4.5-SMD | 31.74 | **BOOT** (the upper of the two) |
| U176 | MIC-SMD 5P | 92.12 | PDM microphone |

> Boot/reset identification is from the user, by inspection, with the Crowtail edge upward. It is
> orientation-dependent — re-check if the board is ever described in a flipped orientation.

Neither button is used in normal operation (the unit flashes over OTA), but **BOOT+RESET is the
only USB recovery path**. Decide deliberately whether the case exposes them; see *Open questions*.

### Inboard (not at an edge)
| ref | package | X | Y | what |
|---|---|---|---|---|
| J5 | 9P-SMD-W/-RING | 17.22 | 15.20 | **microSD / TF slot** — inboard, does not reach a case wall |
| J21 | 30P 0.5 mm FPC | 89.60 | 44.51 | MIPI-DSI to panel |
| J8 | TEST-PIN-4-2.0MM | 139.95 | 38.23 | test header |

## Power tree — VERIFIED from the schematic

Three inputs, all **diode-OR'd into the same `VDD5V` rail** through identical Schottky pairs:

| input | connector | diodes | → |
|---|---|---|---|
| `+VBUS1` | J1 USB-C | D10 DSS34 / D11 1N5817WS | `VDD5V` |
| `+VBUS2` | J16 USB-C | D007 DSS34 / D008 1N5817WS | `VDD5V` |
| **`+5V_IN`** | **J10 pin 3** (XH2.54-4P, right edge) | **D2 DSS34 / D4 1N5817WS** | `VDD5V` |

`J7` pins 2/4/6 are also `VDD5V` (bottom-edge GPIO header), a second viable feed point.

**Consequence, and it drives the whole rear design: J10 is a first-class 5 V input, electrically
equivalent to either USB-C port.** The rear USB-C breakout feeds the board with **two wires into a
keyed XH2.54 connector** — no USB-C plug inside the case, no plug overmold, no cable bend radius.
On a ~22 mm-deep flush-mount case that is the difference between tight and comfortable, and it
supersedes the pigtail-to-USB-C arrangement drawn in the design system's `industrial/side-view.html`.

J10 pinout: `1 = UART_IN_TXD3 · 2 = UART_IN_RXD3 · 3 = +5V_IN · 4 = GND`.

Expect a ~0.4–0.5 V Schottky drop, identical on all three paths, so no regression versus USB-C
power. DSS34 is rated 3 A; the board draws up to 2 A.

> ⚠️ **The USB-C breakout must have 5.1 kΩ CC pull-downs** on CC1/CC2. Without them a USB-C PD
> charger negotiates nothing and delivers no power. Many cheap breakouts omit them.

## Case design decisions settled so far

| | |
|---|---|
| Layout | Control column **right of the screen**, per the design system's front elevation |
| Face | **~230 × 112 mm** (3 wall + 0.75 clearance + 176.90 PCB + 46 column + 3 wall) |
| Mounting | **Keyhole slots cut into the rear shell** — lowest profile, adds 0 mm |
| Wall | Unit sits **flush**; no cavity. A small hole passes the USB-C cable, pushed back in once mated |
| Power | Rear USB-C breakout → 2 wires → **J10** (see above) |
| Controls | I2C rotary encoder board + I2C GPIO breakout for buttons, **daisy-chained to J13** (Crowtail I2C) |
| Cable run | J13 is top-**left** (x = 40), the controls are on the **right** — the rear shell needs a channel across the full width |

Bus addresses already in use on the shared I2C bus: **GT911 touch at 0x5D**, and an
**unidentified device at 0x2F** (see `plans/07-sonos-jukebox.md`). Confirm neither control board
collides.

## Open — measurements still needed

1. **Active-area position on the PCB.** 155 × 87 inside 176.90 × 104.00 leaves ~22 × 17 mm of
   slack; where it lands sets every bezel dimension. Not in the board file — the panel is a bonded
   module. Needs four caliper gaps from the PCB edges to the lit area.
2. **Glass / metal frame overhang** — does the display module extend past any PCB edge, and by how
   much? Sets the front pocket.
3. **Component heights**, front face and rear face. Eagle carries no Z. Sets case depth, standoff
   height and the rear-shell profile.
4. **Breakout board dimensions** — the USB-C breakout, the I2C rotary encoder board and the I2C
   GPIO breakout: outline, hole pattern, connector height.
5. **Encoder shaft** — type, diameter and length proud of its PCB. Sets the Ø36 dial cap and how far
   behind the face the encoder board sits.
6. **Button switch bodies** — sets the Ø13 cap and the column's internal depth.

## Open questions

- **BOOT/RESET access.** Both are on the left edge and are the only USB recovery path. Exposing them
  costs two small holes in a wall-mounted unit that is otherwise sealed; not exposing them means a
  bricked unit comes off the wall and opens up. Not yet decided.
- **microSD access.** J5 is ~17 mm inboard of the left edge, so the card does not reach a case wall.
  If the on-card album-art cache ships (`plans/07-sonos-jukebox.md`), card access needs a recessed
  channel — confirm insertion direction and slot depth first.
- **Print bed.** A ~230 mm face fits a 256 mm Bambu or a 250 mm Prusa. It does **not** fit a 220 mm
  bed, even diagonally; that would force a split face with dovetails.
