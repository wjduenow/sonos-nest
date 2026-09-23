# sonos-button-v3 shell

The `button-v3` case: a Waveshare ESP32-S3-LCD-1.47B behind a screen window, with a FILN
FLM12-FJ-6 illuminated button on top. Firmware and rationale: `plans/14-button-v3.md`.

> **Status: BUILT, NOT PRINTED — 2026-09-09.** Three parts — `body.stl`, `bezel.stl`, `back.stl` —
> all watertight, every clearance row passing, all three pairwise interference tests clean, and six
> access envelopes verified — USB receptacle and plug, button body+tail, nut sweep, nut column and
> the harness slot. **42.17 x 24.98 x 52.32 mm**, 12.08 + 4.01 + 5.19 cm³.
>
> **PRINTED AND ASSEMBLED**, with a board, button, harness and cell fitted and the firmware
> running on it. The mounting height is confirmed — the glass sits flush in the bezel. Nothing has been printed or test-fitted,
> and `PCB_T` is still an assumption rather than a measurement (§2).

## 1. The idea

Same product as `hardware/button-v2`, plus a screen. Landscape: the board's long axis is horizontal
so the display reads landscape, matching `DISPLAY_ROTATION 1` as shipped.

**A three-layer sandwich.** Front to back: `bezel` | board | **integral middle plane** | `back`.

```
  y=0        3.0                9.3   12.0            22.48   24.98
  |  bezel   |  board (glass flush at y=0)  |  cavity  | back  |
             |                    | MIDDLE PLANE |             |
                                  ^ board screws in from HERE, back cover off
```

- **The middle plane is part of the body**, not a separate carrier. It sits 0.5 mm behind the
  tallest component, so the board screws are short (**M2 x 8**), and there is **10.5 mm** of open
  cavity behind it for a screwdriver. Every earlier arrangement failed one of those two — either
  M2 x 18 spanning the whole box, or a joint nothing could reach.
- **The board loads from the FRONT**, through the bezel opening, and lands on four posts standing
  off the plane.
- **The bezel exposes the WHOLE glass** — dead border and driver chin included — with the glass
  surface flush with the frame. Nothing overlaps the display, so nothing can crop a pixel or press
  on one.

Three things set the outside, and none of them is the PCB:

- **Depth is set by the M12 NUT, not the electronics.** The button tightens from inside, so the
  cavity has to swallow 18.48 mm across corners — more than the whole board stack needs.
  `hardware/button-v2` has the identical `max()` for the identical reason.
- **Height is set by the button sitting ABOVE the board.** Its body plus solder tail is 15.00 mm
  and the board spans the full width, so it cannot tuck under. The board's top edge is pinned at
  z = 16, and centring the board then fixes the height at 52.32.
- **Width is the only dimension the PCB wins**, at 36.37 + gaps + walls = 42.17.

> ⚠️ **The BOARD is centred, not the lit area — and that is a reversal.** It was the other way
> round while the bezel covered the glass down to the lit rectangle: what you saw was the picture,
> so the picture got centred. Now the whole glass shows, so the glass is the visible rectangle, and
> centring the lit area instead would leave the opening 1.79 mm off centre with left and right
> frame margins differing by 3.57 mm. The lit area now sits its natural 1.79 mm right of centre
> inside the glass, exactly where this panel's driver chin puts it. It also bought back 3.57 mm of
> width.

## 2. Where every number came from

### From the vendor drawing (`ESP32-S3-LCD-1.47B-details-size.jpg`, read at 3x)

PCB **36.37 x 20.32**; header **2.54** pitch with rows **17.78** apart and the last pad **11.31**
from the right edge; mounting holes explicitly **M2**.

Waveshare publishes **no STEP model** for this board — checked. The only Drive link on the wiki is
the 363 MB Arduino installer.

> ⚠️ **17.78 is the header ROW SPACING, not a hole pitch**, and it reads like one. The arithmetic
> settles it: 17.78 + 2 x 1.27 = 20.32 exactly, i.e. two pad rows inset 1.27 mm from each long
> edge. Taken as a hole pitch it would have put the bottom holes 0.98 mm off the end of the board.

### Measured on the board, 2026-09-08/09

| what | value | note |
|---|---|---|
| glass top -> PCB back face | **5.50** | the LCD is seated on the PCB, so this is the only reachable datum |
| glass top -> tallest component | **8.50** | closes exactly: 5.50 + 3.00 |
| lit area | **32.4 x 17.4** | |
| lit area offset from PCB top-left | **0.2, 0.2** | PCB-relative: LCD and PCB share one footprint |
| dead chin, USB-C side | **3.5** | cross-checks the derived 3.77 right margin |
| M2 hole centres | **x 1.97 / 33.97, z 3.5 / 16.8** | eyelets are THREADED |
| USB-C shell | **9.0 x 3.3**, centred | cross-checks the derived 3.00 tallest component |

Three independent cross-checks closed, which is what makes these trustworthy: the chin against the
derived right margin, the USB-C height against the derived tallest component, and 3.5 + 16.8 =
20.3 against the 20.32 board.

> ⚠️ **`PCB_T` is ASSUMED at 1.6, not measured** — the LCD is bonded to the board, so calipers
> cannot reach bare laminate. The design is made insensitive to it: `GLASS_AIR` is 1.0 so the
> pillars can be 0.3 out without the glass reaching the window rim.

### The measurement that cost a round trip

Asking for "display glass above the PCB's display-side face" was an unanswerable question on this
board, and it produced two readings (5.75, then 5.5) that looked like two quantities in conflict.
They were one quantity twice. **Ask for a dimension between two surfaces a caliper can actually
touch**, not between one surface and a plane buried inside an assembly.

### ⚠️ Superseded — the FLM12 thread is 11.85, not 11.71

`plans/04` §6 recorded the thread major diameter as **11.71**. Re-measured 2026-09-09 on the button
in hand it is **11.85**, and that 0.14 mm mattered: the bore was a typed 12.0, so nominal clearance
was 0.15 — and FDM prints holes **undersize**, which put the printed hole at or below the thread.
Reported from a real print as "a little tight".

The bore is derived now (`BUTTON_BODY_D + BUTTON_BORE_CLR`, 0.45) rather than typed, which is
exactly what let a small change in the thread silently eat the whole margin. Clearance is spent
freely here because the asymmetry is total: the Ø14 flange sits on the outer face and hides the
bore completely, so slop is **invisible**, while 0.1 mm too small is a part you cannot assemble.

> ⚠️ `hardware/button-v2` and `hardware/cam-button` still carry **11.71** with a typed 12.0 bore.
> Same physical button, so they are likely tight too — not changed here, because their STLs are
> committed and may already be printed.

### ⚠️ Superseded — what the drawing got wrong

Tolerance is friendlier than it looks: the M2s get **Ø2.6 clearance holes**, so ±0.3 mm per hole
still assembles. Nothing here needs to be surgical.

| # | what | why it is needed |
|---|---|---|
| 1 | **PCB thickness** | nominally 1.6; sets the boss standoff and the pocket |
| 2 | **Tallest part on the COMPONENT side**, above that PCB face | probably the USB-C shell; sets rear clearance |
| 3 | **Display glass above the DISPLAY-side PCB face** | sets how far the board stands off the front wall |
| 4 | **Lit area W x H** | expect ~32.4 x 17.4 — confirm, don't assume |
| 5 | **Lit area offset** from the PCB's left and top edges | the window position. Cannot be derived at all, and a window that crops the display is a reprint |
| 6 | **USB-C**: overhang past the board edge, shell W x H, centreline from the top edge | the side-wall cutout |
| 7 | **All four M2 hole centres**, from the PCB's top-left corner | see below |

> ⚠️ **The drawing suggests the holes are NOT symmetric.** Top-left reads 1.97 from the left edge
> and **3.52** from the top edge; top-right reads 2.40 from the right edge and **2.00** from the
> top. A 1.5 mm difference in Y between two corners of the same rectangular board is odd enough
> that it is either real — in which case assuming symmetry puts two bosses 1.5 mm out — or an
> artefact of reading leaders off a JPEG. Measure all four; do not mirror one pair.

`button_params.py` refuses to build while any of these is unset: they are `MEASURE(...)` sentinels
that raise on first arithmetic, rather than plausible defaults. A wrong-but-reasonable number is
the failure this repo keeps paying for — the FLM12 depth was 14.0 measured against a datasheet
that supported two different readings, and `hardware/cam-button` still carries the old value.

## 3. Files

```
shell/button_params.py   single source of truth
shell/build_body.py      body.stl + check_clearances()  (26 rows)
shell/build_bezel.py     bezel.stl + a countersink-actually-cut assertion
shell/build_back.py      back.stl + all three pairwise interference assertions
shell/build_all.py       runs all three, in order
shell/render_preview.py  render_preview.png
```

> ⚠️ **Nor can a dimensional check catch an ACCESS fault.** Whether a cable can physically reach
> the socket is a question about the space *between* parts, and no dimension on the body describes
> it. The first sandwich build passed every numeric row while the lower-right post was buried
> 0.09 mm in the connector body and the notch was 2 mm too shallow for a plug's overmold.
> `build_body.py` now intersects the body with modelled **receptacle**, **plug**, **button
> body+tail**, **nut sweep**, **nut column** and **harness band** envelopes, and asserts all six
> are empty.
>
> Running that on the button immediately found a second one of the same shape: the middle plane
> started 2 mm above the board's top edge, at z = 14, and the button's solder tail reaches z = 15 —
> so the plane was sitting **inside the button**. Both the bore diameter and the nut's swept circle
> were checked numerically and both passed, because a dimension on the bore says nothing about what
> is 12 mm further down the same axis.
>
> ⚠️ **The envelope has to be the real part, not the part plus a safety margin.** Modelling the
> button body as thread+clearance reported the bore's *intended* close fit (12.0 on an 11.71
> thread) as a collision. Modelling the nut's insertion as a sliding box reported the box's square
> corners against a round relief the hexagon never reaches. Both were false alarms from a padded
> envelope, and a false alarm you then "fix" is worse than no check at all.
>
> Two things came out of that, and both are the kind of constraint that only shows up once:
> `POST_OD` is 4.0 rather than 4.5 because the lower-right eyelet is 2.16 mm from the edge of a
> 9 mm connector — set by the connector, not by the cavity wall it looks like it should be. And the
> USB notch grows **forward** toward the bezel rather than backward, because backward is blocked by
> the middle plane sitting 0.5 mm behind the shell, which left only 4.8 mm of usable opening.

> ⚠️ **A per-part check cannot catch an assembly fault.** An early build had both parts watertight,
> both passing every clearance row, and **0.064 cm³ of solid in the same place**. `build_back.py`
> now asserts all three pairwise intersections are empty on every build — body∩bezel, body∩back,
> bezel∩back. Keep them.

Built with Python CSG (trimesh + manifold3d), never OpenSCAD — see `hardware/README.md`:

```bash
conda run -n img23d python hardware/button-v3/shell/button_params.py   # print the derivations
conda run -n img23d python hardware/button-v3/shell/build_all.py       # once it exists
```

## 4. Bill of materials

### Bought

| qty | part | notes |
|---|---|---|
| 1 | **Waveshare ESP32-S3-LCD-1.47B** | ⚠️ the **B** matters — see §2. 36.37 x 20.32 mm, ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 1.47" ST7789 172x320, QMI8658 IMU, USB-C |
| 1 | **FILN FLM12-FJ-6** | Ø12 momentary, IP67, metal, **white** illuminated ring. Four flying leads: white, black, brown, brown |
| 1 | **LiPo cell, 3.85 V** | the one fitted is 550 mAh / 2.12 Wh with three leads (+, −, NTC). Caveats below |
| 4 | **M2 x 8** pan head | board → middle plane, driven from the REAR with the back cover off |
| 8 | **M3 x 8** countersunk | four front into the bezel, four rear into the back cover — into the **same** four bosses |
| — | heatshrink | for the cell's unused NTC lead |

The button ships with its own harness, so it needs no extra wire. The cell's leads solder straight
to the board's `VBAT` / `GND` pads.

> ⚠️ **The M2 length is pinned from BOTH ends and is asserted, not chosen.** Under 1.0 mm of
> engagement it does not hold; past `PCB_T` it drives through the board into the back of the LCD,
> which nothing recovers. A brass eyelet in a 1.6 mm board offers at most 1.6 mm of thread, so the
> window is narrow — `MID_T` is tuned to 2.7 precisely so a **stock M2 x 8** lands inside it
> (crosses 6.50, engages 1.50). Change `MID_T` and the screw length changes with it.

One boss serves both covers, with a pilot drilled in from each end — deliberately not through, or
a screw driven from either side would push past its own thread engagement.

### Printed — `shell/`

| part | volume | what it does |
|---|---|---|
| `body.stl` | 12.08 cm³ | four walls, closed rear, the integral middle plane and the four board posts |
| `bezel.stl` | 4.01 cm³ | front frame; the glass sits flush in its opening |
| `back.stl` | 5.19 cm³ | comes off to reach the M2 screws |

**≈ 21.3 cm³ total, ~26 g in PLA.** Print flat, no supports. Assembled box is
**42.17 x 24.98 x 52.32 mm**.

Optional, `gauge/` — `hole_gauge.pdf` (print at 100%) or the three `.stl` gauges, for checking the
mounting-hole pattern before committing to a case print. See §6.

### Wiring

```
button                 board, LEFT header rail (USB-C end first)
  white  ──────────►   pad 1   5V (VBUS)      ring +
  brown  ──────────►   pad 2   GND            switch
  brown  ──────────►   pad 5   GP2            switch   (either brown; no polarity)
  black  ──────────►   pad 7   GP4            ring −   (the pin SINKS it)
                       pad 6   GP3            ⚠️ LEAVE EMPTY — JTAG strapping pin

cell                   board, RIGHT header rail
  red    ──────────►   pad 3   VBAT
  black  ──────────►   pad 4   GND
  white  ──✂           NTC — unused, insulate
```

> ⚠️ **The ring is wired LOW-SIDE, and that is the whole trick.** A white LED has Vf ≈ 3.1 V and
> the ring is specced 5–24 V, so a 3.3 V pin **cannot source it**. It does not need to: white goes
> to the full 5 V and black lands on the GPIO, which pulls the cathode to ground. No MOSFET. The
> logic inverts as a result — **LOW = lit**.

> ⚠️ **Meter the cell's leads before soldering.** These are bare pads, not a polarised connector.
> Two pairs read ≈3.8 V and one reads ≈0 V; the lead *not* in the 0 V pair is **+**. Reversing +
> and − is the one destructive mistake. Guessing wrong between − and the NTC is harmless — the
> return would run through a ~10 kΩ thermistor, so the board simply will not power up.

### Things the BOM cannot fix

- **The ring goes dark on battery.** It hangs off VBUS, which exists only while USB is connected.
  The screen wakes on a press instead, so press feedback survives in a different form.
- **Runtime is ~5 h.** The always-associated radio dominates and the screen's duty cycle barely
  moves it. This is a UPS, not a cordless product, without light-sleep work.
- **The cell is a 3.85 V type on a 4.2 V charger**, so it takes roughly 80% of its rated capacity.
  That is the safe direction of mismatch — undercharging, not overcharging.
- **The CHG indicator LED cannot be turned off in firmware.** It is wired to the charge-management
  IC, not to a GPIO, and lights whenever a cell is charging. Mask it if the glow shows through the
  case. The RGB bead *is* driven dark at boot — see `pins.h`.

## 5. Still open

1. ⚠️ **The board posts assume the PCB is clear of components around each eyelet.** They stand off
   the middle plane, are 4.0 mm across and land on the board's back face at the corners. Never
   verified against a populated board — if a post lands on a capacitor the board will sit crooked
   in the window, which is the symptom to recognise.
2. ⚠️ **`PCB_T` is assumed 1.6.** Absorbed by `GLASS_AIR` if it is out by <=0.3; beyond that the
   glass moves toward the window rim.
3a. ⚠️ **The harness has exactly one route, and it is 1.5 mm.** The four button wires leave the
   header row at z = 17.27 and must run along the band between the board's top edge and the middle
   plane. There is 0.4 mm beside the board and 0.5 mm behind it — nowhere else at all. The plane's
   top edge is therefore pinned to the upper posts' base (z = 17.5) and cannot move up. Asserted,
   but tight, and untested with real wire.
3. ⚠️ **`HOLE_X1` comes off a drawing, not a caliper.** The 2.6 pillar bore gives 0.3 of radial
   slop. If the first print will not take all four screws, this is the number to re-measure.
4. **Whether the ring's 5 V low-side drive works off this board's header** — unproven, and the
   last thing that could still change the BOM (`plans/14` §Open).
4b. **The glass sits flush in the bezel opening with 0.15 mm all round.** Nothing presses on it,
   but that also means nothing hides a misalignment — if the board sits proud or shy of flush, it
   will show. First print will tell.
5. ~~Only two lid screws, both at the top~~ — **four now**, one near each corner, once the box
   grew to centre the screen. The upper pair still has to dodge the M12 nut's 19.48 swept circle.
6. **No USB-C strain relief.** `button-v2` needs pinch ribs because the XIAO has no mounting
   holes; here the board is screwed down at four corners, so the load path is probably fine — but
   it is untested.

## 6. Locating the mounting holes — `gauge/`

The hole positions are the least-verified numbers in the build: the **Z centres are measured**
(3.5 / 16.8, and 3.5 + 16.8 = 20.3 against a 20.32 board proves they are true centres), the **X
pitch is agreed** by drawing and caliper (32.00), but the **X absolute position comes from the
vendor drawing alone** and has never been checked.

> ✅ **RESOLVED 2026-09-15 — THE FOUR HOLES ARE A TRAPEZOID, NOT A RECTANGLE.**
> The pair flanking the USB-C sit **further apart** than the pair at the far end. Measured on
> hardware (~15.5 at the USB-C end against ~12 at the other), and all three documentary sources
> agree once read correctly:
>
> ```
> X:  36.37 - 1.97 - 2.40 = 32.00   the long-axis pitch, as printed
> Z:  20.32 - 2 x 3.52    = 13.28   the pitch at the FAR end, as printed
> Z:  20.32 - 2 x 2.00    = 16.32   the pitch at the USB-C end, as printed
> ```
>
> The photo-drawing's four insets — 1.97, 2.40, 3.52, 2.00 — are **four different dimensions**,
> not four readings of one pattern. The sibling's mechanical drawing
> (`ESP32-S3-LCD-1.47_mechanical_NON-B.pdf`, a true vector drawing parsed straight off its path
> data) has row spreads of exactly 16.32 and 13.28. Same trapezoid.
>
> ⚠️ **Two wrong turns were taken here, both by assuming symmetry**, and they are worth recording
> because the reasoning looked sound each time:
>
> 1. The 2.00 inset was written off as "a misread leader", because 3.52 against 2.00 on one board
>    seemed implausible.
> 2. The measured `3.5 + 16.8 = 20.3` was then read as *proof* of symmetry. It is not — it only
>    proves the **far-end pair** is centred about the long axis, which it is. Both pairs are
>    individually centred; the two pairs simply have different spreads.
> 3. The sibling drawing's 16.32 row was even dismissed as the USB-A shell's width. It was the
>    holes.
>
> **The general lesson: on that photo-drawing, a dimension that looks redundant with another is
> probably measuring a different feature.** `HOLES` is now an explicit list of four `(x, z)` pairs
> rather than a cross product of two axes — a cross product cannot express a trapezoid, and
> writing one is exactly how the USB-C pair ended up 1.5 mm too close together.

> ⚠️ **The 1.47B's own documentation is almost nothing.**> ⚠️ **The 1.47B's own documentation is almost nothing.** There is no STEP model, no hole table and no
> mechanical PDF — just one annotated photo giving `M2`, the 36.37 x 20.32 outline, and four
> insets (1.97 / 2.40 / 2.00 / 3.52) whose leader lines are ambiguous. Two of them even conflict:
> top-left reads 3.52 from the top edge while top-right reads 2.00. Magnifying the drawing
> confirms **3.52 is top-edge-to-centre** (matching the measured 3.5); the 1.97 leader stays
> unreadable at any magnification.

**Do not reach for calipers.** The centre of a hole is not a feature a jaw can touch, and
measuring to its near edge is exactly what produced the bogus "1 and 33" reading — 1.97 and 33.97
each one hole-radius short.

Two better tools, both driven off `shell/button_params.py` so they always show what the model
currently believes:

```
conda run -n img23d python gauge/build_paper_gauge.py   ->  hole_gauge.pdf   (seconds)
conda run -n img23d python gauge/build_gauge.py         ->  three .stl       (minutes)
```

**The paper sheet is the one to reach for first**, and it works because the eyelets are
THROUGH-holes: lay the board on the printed outline and each eyelet becomes a ~2 mm window onto
the paper. A bullseye is printed where the model thinks the hole is, and **the heavy 1.0 mm ring
is the M2 bore's own radius** — so on a correctly modelled hole it sits exactly under the eyelet's
rim all the way round. Gap on one side, hidden on the other, means that hole is off, that way, and
the 0.25 mm rings size it.

> ⚠️ **Print at exactly 100%.** "Fit to page" scales by a few percent, which is the same order as
> the error being hunted. The sheet carries a 100 mm check bar — measure it first.

What the pattern of errors means, which is why seeing all four at once beats four separate
readings:

| observation | cause |
|---|---|
| all four off the same way | the pattern needs shifting — an offset |
| off in opposite directions | the pitch is wrong, not the position |
| only one off | the board is not square on the outline; re-seat it |

The STL gauges are the same idea in plastic — `check` (3.6 mm holes, always drops on, shows the
error), `fit` (2.6, the real clearance), `tight` (2.2, seats only if the pattern is right to
~0.1 mm). A notch marks the USB-C edge so a plate cannot be laid on flipped and hide an
asymmetric error.
