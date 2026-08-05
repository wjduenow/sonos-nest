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

### ✅ RESOLVED — Eagle's "top" layer is the physical REAR. The front view is MIRRORED.

Confirmed two ways: face-down on a flat table the unit **rests on the glass** (so nothing on the
display face stands proud), yet the **thickest point is the Crowtail I2C connector** — which
therefore cannot be on the display face. And directly, by inspection: screen facing the viewer with
the Crowtail connectors along the upper long edge, **the two USB-C ports are on the LEFT**.

So the display is bonded to Eagle's *bottom* and every Eagle X coordinate is mirrored when viewed
from the front:

```
    X_front = 176.90 − X_eagle          (Y is unchanged)
```

**Use the front-view table below for all case geometry.** The raw Eagle values are kept for
traceability, but reading them directly will put every feature on the wrong side.

Happily, the **mounting-hole pattern is mirror-symmetric** — (3, 3) / (3, 101) / (173.90, 3) /
(173.90, 101) are the same in both frames. Only the connectors move.

### Front view — screen facing the viewer, Crowtail edge up, X from the LEFT edge

| feature | X_front | Y | side of the face |
|---|---|---|---|
| SW1 slide switch (unused) | 4.81 | 79.45 | far left |
| **J1 USB-C** | 6.52 | 62.47 | far left |
| **J16 USB-C** | 6.52 | 41.57 | far left |
| **J10 — +5V\_IN** | **6.17** | **19.80** | **far left, low** |
| J6 PH2.0 | 15.97 | 3.11 | bottom left |
| J11 radio header | 16.23 | 91.00 | top left |
| J3 PH2.0 | 31.97 | 3.11 | bottom left |
| J9 radio header | 31.73 | 91.00 | top left |
| J8 test header | 36.95 | 38.23 | left of centre |
| FPC2 (6P) | 50.85 | 95.18 | top, left of centre |
| J21 DSI (30P) | 87.30 | 44.51 | centre |
| FPC3 (24P) | 88.40 | 94.90 | top centre |
| J7 24-pin GPIO header | 88.47 | 6.64 | bottom centre |
| **J13 — Crowtail I2C** | **136.90** | **96.57** | **top, right of centre** |
| J4 PH2.0 | 141.18 | 3.11 | bottom right |
| J2 Crowtail UART | 158.90 | 96.57 | top right |
| **J5 microSD slot** | **159.68** | **15.20** | **right of centre, low** |
| K3 **BOOT** | 172.62 | 31.74 | **right edge** |
| K4 **RESET** | 172.62 | 17.25 | **right edge** |
| U176 PDM mic | 173.86 | 92.12 | right edge, high |

**This is a good layout for the chosen design.** The control column is on the right, and the mirror
puts the **Crowtail I2C connector, the microSD slot and both BOOT/RESET buttons on the column side** —
short cable runs, and a plausible route for hidden SD and recovery access through the column cavity.
Power is the odd one out: **J10 sits at the far left**, opposite the column.

## Outline

| | |
|---|---|
| PCB outline | **176.90 × 104.00 mm** (vendor docs say "180 × 105" — that is nominal, not the drawing) |
| Corner radius | **3.0 mm**, all four |
| Active display area | **155 × 87 mm** (vendor docs; position on the PCB **VERIFY**) |
| PCB thickness | **1.65 mm** (measured) |

## Mounting holes — 4× M3.2 through

| ref | X | Y |
|---|---|---|
| J18 | 3.00 | 3.00 |
| J12 | 3.00 | 101.00 |
| J14 | 173.90 | 3.00 |
| J15 | 173.90 | 101.00 |

**Bolt pattern 170.90 × 98.00 mm**, 3.0 mm in from each corner.
Package `THO-M3.2*5MM-HOLE` — 3.2 mm drill, 5 mm pad.

## Connector map, by edge — ⚠️ EAGLE (REAR-VIEW) FRAME

> These are the raw Eagle coordinates, kept for traceability against the vendor files.
> **Left/right are reversed from what you see looking at the screen.** For case geometry use the
> front-view table above.

### Eagle-RIGHT edge (x ≈ 170–177) — appears on the LEFT of the front view
| ref | package | Y | what |
|---|---|---|---|
| J10 | XH2.54-4P-SMD | 19.80 | UART3 TX/RX + **+5V\_IN** + GND — **the power input this case uses** |
| J16 | USB-3.0TYPEC-SMD | 41.57 | USB-C #2 (`+VBUS2`) |
| J1 | USB-3.0TYPEC-SMD | 62.47 | USB-C #1 (`+VBUS1`) |
| SW1 | SWITCH_6PIN_2P | 79.45 | 2-position slide switch — **unused; enclosed, no access** |

> SW1's function was never identified (no nets resolved for it in the schematic pass). It is being
> covered deliberately. **Set it to the working position and confirm the unit boots before closing
> the case** — if it turns out to gate power or boot mode, it is unreachable afterwards.

### Eagle-TOP edge (y ≈ 91–97) — the TOP of the front view too (Y is not mirrored)
| ref | package | X | what |
|---|---|---|---|
| J2 | CONNECTOR_4P-SMD-2.0 | 18.00 | Crowtail **UART** (RXD1/TXD1/3V3/GND) |
| J13 | CONNECTOR_4P-SMD-2.0 | 40.00 | Crowtail **I2C** (SCL/SDA/3V3/GND) — **control-board daisy chain** |
| FPC3 | FPC 24P 0.5 mm | 88.50 | — |
| FPC2 | FPC 6P 0.5 mm | 126.05 | — |
| J9 | 7-pin 2.54 | 145.17 | radio module (SPI2, VDD5V_W) |
| J11 | 7-pin 2.54 | 160.67 | radio module (I2C1, IO9/IO10) |

### Eagle-BOTTOM edge (y ≈ 3–7) — the BOTTOM of the front view too
| ref | package | X | what |
|---|---|---|---|
| J4 | PH2.0WT-2PIN | 35.72 | speaker / power |
| J7 | 2X12-SMD1 | 88.43 | **24-pin GPIO header** (P4_IO3/4/5/25/27/28/49/50/51/52, VDD5V, 3V3, GND) |
| J3 | PH2.0WT-2PIN | 144.93 | speaker / power |
| J6 | PH2.0WT-2PIN | 160.93 | speaker / power |

### Eagle-LEFT edge (x ≈ 3–5) — appears on the RIGHT of the front view
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
> **✅ The selected breakout has them** — see `README.md`.

## Measured thickness — the depth budget

Measured on the unit, not derived:

| | |
|---|---|
| **Total electronics envelope** | **16.5 mm** — glass front surface → outermost face of the Crowtail I2C connector on the rear |
| Tallest point | the **Crowtail I2C connector**, rear face, top edge |
| PCB | **1.65 mm** |
| microSD slot | protrudes **1.5 mm** past the PCB's rear face — well inside the 16.5 mm envelope |
| Display face | **nothing stands proud of the glass** (rests flat, face-down) |

Which gives a comfortable case:

```
   1.5   front bezel lip over the glass
  16.5   electronics envelope
   0.5   clearance
   2.5   rear wall
 ──────
  21.0   mm total   (design token allows 22)
```

> ⚠️ **The corollary that shapes the rear shell: only ~3 mm sits behind the PCB.** Anything needing
> real depth at the rear — a keyhole screw-head pocket, the USB-C breakout — **cannot go anywhere the
> PCB is**. See *Open questions*.

## Case design decisions settled so far

| | |
|---|---|
| Layout | Control column **right of the screen**, per the design system's front elevation |
| Face | **~230 × 112 mm** (3 wall + 0.75 clearance + 176.90 PCB + 46 column + 3 wall) |
| Depth | **~21 mm** (see above) |
| Mounting | **Keyhole slots cut into the rear shell** — lowest profile, adds 0 mm |
| Wall | Unit sits **flush**; no cavity. A small hole passes the USB-C cable, pushed back in once mated |
| Power | Rear USB-C breakout → 2 wires (VBUS, GND) → **J10**, which sits at the **far LEFT** of the front view |
| Controls | I2C rotary encoder board + I2C GPIO breakout for buttons, **daisy-chained to J13** (Crowtail I2C) |
| Cable run | **Short** — J13 is at front-x ≈ 137, the control column starts at ≈ 180. The *power* run is the long one, from the breakout to the far-left J10 |

Bus addresses already in use on the shared I2C bus: **GT911 touch at 0x5D**, and an
**unidentified device at 0x2F** (see `plans/07-sonos-jukebox.md`). Confirm neither control board
collides.

## Open — measurements still needed

1. **Active-area position on the PCB — the blocker on the face.** 155 × 87 inside 176.90 × 104.00
   leaves ~22 × 17 mm of slack; where it lands sets every bezel dimension. Not in the board file —
   the panel is a bonded module. Needs four caliper gaps from the PCB edges to the **lit** area
   (screen powered), which also cross-checks the vendor's 155 × 87.
2. **Glass / metal frame overhang** — does the display module extend past any PCB edge, and by how
   much? Sets the front pocket.
3. **Edge-connector heights and outward overhang** — how far the USB-C, XH2.54 and Crowtail bodies
   stand above the PCB face, and whether any sits proud of the PCB outline. Sets the side walls.
4. **I2C rotary encoder board** — outline, hole pattern, connector height, **and its I2C address**.
5. **I2C GPIO breakout** — outline, hole pattern, connector height, and its I2C address.
6. **Encoder shaft** — type, diameter and length proud of its PCB. Sets the Ø36 dial cap and how far
   behind the face the encoder board sits.
7. **Button switch bodies** — sets the Ø13 cap and the column's internal depth.

✅ Resolved: PCB thickness, total envelope, front-face clearance, microSD protrusion, board
orientation, printer bed, USB-C breakout dimensions.

## Open questions

- **Where the depth-hungry parts go.** Only ~3 mm sits behind the PCB, so keyhole screw-head pockets
  and the 4.75 mm-thick USB-C breakout must live where the PCB does not reach. Today that is only
  the 46 mm control column on the right — but hanging a 230 mm-wide unit from keyholes clustered in
  its right-hand strip would let it swing. Candidate fixes: take the case to ~24 mm deep; grow the
  face to ~124 mm to create mounting bands above and below the PCB; or keyholes in the column plus a
  hooked lip at the lower left. **Not yet decided — this is the next real decision.**
- **BOOT/RESET access.** Both are on the **right** edge in front view (front-x ≈ 172.6), i.e. right
  against the control column, so a hidden port through the column cavity is plausible. They are the
  only USB recovery path. Not yet decided.
- **microSD access.** J5 is at front-x ≈ 159.7 — ~17 mm inboard of the **right** edge, again on the
  column side. If the on-card album-art cache ships (`plans/07-sonos-jukebox.md`), card access needs
  a recessed channel; confirm insertion direction and slot depth first.
## Print constraints — resolved

**Bambu P2S, 10" × 10" (~254 × 254 mm) bed.** The ~230 × 112 mm face prints **flat, in one piece**,
with ~24 mm of margin in X. No split face, no dovetails. The taller-face mounting variant
(~230 × 124) and a 24 mm-deep shell both still fit comfortably.
