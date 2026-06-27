# CrowPanel 2.1" Rotary Display — Physical / Mechanical Spec

Mechanical reference for designing **enclosures, mounts, brackets, stands, or any
hardware** that attaches to the unit. This is the *physical* counterpart to the
electrical pinout in `../CLAUDE.md` and `../src/board_pins.h`.

**Unit:** ELECROW CrowPanel 2.1" HMI ESP32 Rotary Display, model **DHE03921D**
(*CrowPanel 2.1inch-HMI ESP32 Rotary Display 480×480 IPS Round Touch Knob Screen*).

> ⚠️ Elecrow publishes **no** mechanical drawing / STEP for this board. The
> dimensions below were reverse-engineered from a community wall-mount STL
> (`wall-mount/reference_mount.stl`) plus the published 79×79×30 envelope, and
> **validated 1:1 against the physical unit** (paper templates — screw pattern
> and Ø58/Ø79 diameters confirmed). Treat ✅ as verified, ◾ as published-only.

## The one rule that governs every design

🚫 **The front bezel / knob ROTATES — it is the rotary input. Never let any part
of an enclosure touch, clamp, or overhang the rotating front face**, or you jam
the knob. Mount the unit **from the rear only**: by the 3 rear screw holes and/or
by cradling the round rear body. The Ø79 front overhangs and hides a rear mount
up to ~Ø61 — use that to keep hardware invisible from the front.

## Key dimensions

| Feature | Value | Conf. |
|---|---|---|
| Overall envelope (Elecrow) | 79 × 79 × 30 mm, ~80 g | ◾ |
| **Front bezel** (round, rotates) | **Ø79 mm** — outer face, overhangs the body | ✅ |
| **Rear body** (round, static) | **Ø58 mm** — the cylinder a cradle cups | ✅ |
| Body depth (face → back) | ~30 mm | ◾ |
| Screen | 2.1" round IPS, 480×480, ST7701 | ◾ |
| **Rear mounting holes** | **3 × on a Ø12 mm bolt circle, 120° apart**, M3 thread, in a **raised center hub** | ✅ |
| **Rear power/data port** | **4-pin MX1.25 JST** (power + programming) on the BACK — **NOT USB-C**; powered via the included MX1.25→USB-A cable | ✅ |
| Mount that stays hidden | any rear mount ≤ **~Ø61** is hidden by the bezel | ✅ |

### Rear screw pattern (exact)

Three M3 holes on a **Ø12 mm bolt circle** in the raised center hub. Idealized:
120° apart. Exact offsets extracted from the reference mount, relative to the
body's geometric center (mm):

```
  hole 1:  (-5.05, -0.04)
  hole 2:  (+3.98, +5.45)
  hole 3:  (+3.98, -5.02)
```

The pattern centroid is ~(+0.97, +0.13) mm — i.e. the bolt circle is offset ~1 mm
from the body axis (real, not noise — it validated on paper). When laying out a
new bracket, use these **exact offsets**, not a perfectly-centered Ø12 circle.
A bracket hole of **Ø4** gives clearance for an **M3** screw passing through into
the hub threads. Screw length: enough to pass your bracket web (~3 mm) + bite.

### Cross-section (rear-mount geometry)

```
        Ø79 front bezel (ROTATES) ── overhangs, keep clear ──┐
   ╔══════════════════════════════════════════════════════╗ │
   ║                  round screen 480×480                 ║ │  front
   ╚══════════════════════╦══════════════════════╦═════════╝
        Ø58 rear body ────╫─ (cups into a cradle)╫               back
                          ║   raised center hub  ║
                          ║   3× M3, Ø12 BC ──────╫─ screws in from behind
                          ╚══ MX1.25 JST on back ═╝  (thin connector + cable)
```

## How to mount it (proven pattern)

1. **Cradle the Ø58 rear body** in a cup (locates + supports it), OR rely on the
   3 hub screws alone for a minimal back-plate.
2. **Fasten through the 3 rear holes** (M3, exact offsets above) into the hub.
3. Keep the whole rear assembly **≤ Ø61** so the Ø79 bezel hides it.
4. **Nothing forward of the rear body** — the bezel must spin freely.
5. The **MX1.25 JST cable** exits the **back** — leave a center/rear opening for
   it (no side cutout needed; a small Ø11 hole clears the thin connector + cable).

The existing wall mount in `wall-mount/` is a worked example of all of this:
a bayonet-coupled wall plate + a cradle that cups the body and screws to the hub.
See `wall-mount/README.md`. Its display-fit geometry is reusable for other parts.

## Provenance / re-verification
- Source of the screw pattern + cup: `wall-mount/reference_mount.stl` (a
  community mount for this exact display), analyzed in `wall-mount/reference_holes.png`.
- Validated with `wall-mount/templates.pdf` (1:1 paper templates) against the
  physical unit — screw pattern ✅, Ø58 body ✅, Ø79 bezel ✅. Rear port confirmed
  as the **MX1.25 JST** (not USB-C) by the owner.
- If a future unit revision differs, reprint `templates.pdf` (100% scale) and
  re-measure; update the values here.

## Manufacturer-documented (Elecrow wiki / product page only)

Elecrow publishes **no mechanical drawing, STEP, or mounting spec** for this unit.
(The two "datasheet" PDFs in their product folder are red herrings: the HMI manual
covers the rectangular Basic series, and `esp32-s3_datasheet_en.pdf` is just the
generic Espressif chip datasheet.) What they *do* document:

- **Housing:** aluminum alloy + plastic, **acrylic** front panel. Envelope 79×79×30 mm, 80 g.
- **Screen:** 2.1" round IPS, 480×480, **400 nits**, capacitive touch.
- **Power:** 5 V / 1 A via the **ZX-MX 1.25-4P** connector (also the program/burn interface).
- **Connectors:** UART0 + UART1 (ZX-MX 1.25-4P), I²C (ZX-MX 1.25-4P), **FPC 12-pin @ 0.5 mm pitch**.
- **Buttons:** Reset, Boot, Confirm (the knob-press switch).
- **LEDs:** power indicator + ambient LED.
- **In box:** 50 cm **MX1.25 → USB-A** cable, 4-pin Dupont cable.

Positions/layout of these connectors are **not** documented — only their existence.
