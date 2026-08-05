# jukebox-7 / wall — flush wall case

Two printed parts for the sonos-jukebox 7" unit: a rear **shell** that holds the board and
carries the wall mount, and a front **face** plate with the screen opening and the control column.

```bash
conda run -n img23d python build_all.py         # -> shell.stl, face.stl
conda run -n img23d python render_preview.py    # -> assembly_preview.png
conda run -n img23d python check_clearances.py  # asserts nothing collides; exit 1 on failure
```

**Run `check_clearances.py` after touching any parameter.** It exists because a face-plate screw
boss was once sitting on top of the PCB's bottom-left mounting boss — and underneath the PCB
itself. That is invisible in an STL until you happen to look into the corner.

All geometry comes from [`case_params.py`](case_params.py). Board numbers come from
[`../crowpanel-p4-7-physical-spec.md`](../crowpanel-p4-7-physical-spec.md).

![preview](assembly_preview.png)

## The design

| | |
|---|---|
| Face | **230 × 128 mm**, R14 corners |
| Depth | **22.0 mm** — lands exactly on the design system's `--u7-depth` token |
| Screen | 155 × 87 opening, 1 mm rebate onto the black border |
| Column | 45.6 mm wide on the right: Ø36 dial at the top, 2×2 Ø13 buttons below |
| Mount | 2 keyholes in the top band, **140 mm apart**, 5.5 mm drop |
| Power | USB-C breakout on edge at the bottom of the column → 2 wires to J10 |
| Print | Bambu P2S; both parts lie flat, face printed front-face-down |

### Depth stack

```
   0.0  rear outer plane (against the wall)
   2.5  interior floor            rear wall 2.5
   3.0  rear-most component       + clearance 0.5
  19.5  glass front plane         + envelope 16.5  (MEASURED)
  22.0  face plate outer          + face 2.5
```

### Why the face is 128 mm and not 112

Only ~3 mm sits behind the PCB, so **nothing that needs depth can go where the board is** — not a
keyhole's captured screw head, not the 4.75 mm USB-C breakout, and **not a face-plate screw boss**,
which runs the full interior height.

The face therefore needs a band above *and* below the PCB. The top band (12.5 mm) carries the two
keyholes. The bottom band was first drawn at 6 mm, which left only 3.75 mm of free strip — too thin
for any boss, so the entire bottom edge of the face plate had no fixing for 215 mm, under a 155 mm
screen opening with a ~7 mm strip beneath it. At **10 mm** it takes proper bosses.

**Face screws are in the bands, three across each** (`FSCREW_X`), never over the PCB, and clear of
the keyholes and the USB-C plate. `check_clearances.py` asserts all of it.

### Why keyholes, and why they don't rock

The unit sits **flush to the wall**, so pressing a button pushes it *into* the wall rather than
levering it off. The two keyholes only have to stop it sliding and swinging, which 140 mm of
separation does comfortably. No cleat, no backplate, **0 mm added depth** — which was the brief.

### The USB-C breakout, and why its datum is the rear plane

The board stands **on edge** with the receptacle pointing at the wall, so its 14.5 mm axis runs
front-to-back:

```
   0.0  receptacle mouth   flush with the rear plane, against the wall
  14.5  rear edge of the breakout PCB
  19.5  face plate inner surface
        ─────────────────────────────
   5.0  mm headroom
```

The receptacle **nests into the 2.5 mm rear wall** rather than competing with it. Seating the board
on the interior floor instead would recess the mouth 2.5 mm and force the plug's overmold into the
port cutout before it could seat — the correct datum is `UC_Z0 = 0`, the outer rear plane.

It mounts on a **single bare plate**, screwed through its two post holes — not trapped in a slot,
and with **no lip or roof over the board**. The two screws run along **X**, so a driver can only
reach them from the side; anything overhanging the board's outer face sits directly in its path.

Which face of the plate the board lands on is `UC_BOARD_SIDE` (+1 / −1). The port centreline stays
on `UC_CX` either way, so flipping it moves only the board and the plate — not the hole you drill in
the wall.

> **Flipping alone does not buy clearance.** `UC_CX` is the column centre, so the two sides are
> mirror images: 21.25 mm of open column on the screw side either way. To actually gain room, shift
> `UC_CX` *away* from the screw side. With the board on the −X face, moving the port to x ≈ 214
> opens the screw side to **~31 mm** — but it also moves the wall's cable hole ~10 mm right, so it
> is a deliberate choice rather than a default. `check_clearances.py` asserts at least 12 mm and
> warns below 18.

The plug then behaves the way the install wants: its nose travels ~6.5 mm into the receptacle inside
the case while the **overmold stays in the wall's cable hole**, so the cable pushes back in once
mated. The wall hole only has to clear the overmold, not the whole connector.

### Power routing

`J10` (`+5V_IN`) is at the **far left** (front-x ≈ 6.2), the breakout is at the **bottom right** of
the column. The two-wire run crosses the width in a recessed channel in the interior floor so the
pair is not pinched under the PCB. The rear port is deliberately **oversized with a 4 mm outward
funnel**, so the hole drilled in the wall does not have to be precisely placed.

## ⚠️ Provisional — do not print the face for final fit yet

**`AA_X0` / `AA_Y0` are a centred guess.** Where the lit area actually sits on the PCB has not been
measured, and it is the only thing that moves the screen opening. Measure the four gaps from the PCB
edges to the lit area (screen powered), set those two values, flip `AA_MEASURED = True`, and rebuild.
The shell is unaffected.

Also VERIFY before a final print, per `hardware/README.md` convention:

- **`REAR_COMP_H`** (5.5 assumed) — PCB rear face to the rear-most component. Only the **boss
  height** depends on it; everything else references the measured glass plane.
- **USB-C breakout hole Ø and centres** — photo-scaled, not measured.
- **Edge-connector heights and any outward overhang** past the PCB outline — sets the side walls.

### The encoder board sits on standoffs, not on the floor

The Arduino Modulino Knob carries a Bourns PEC11J-9215F-S0015, whose shaft is **15 mm from the
bushing flange** on top of a ~6.5 mm body — roughly **21.5 mm from the Modulino PCB to the shaft
tip**. Mounted flat on the case floor the shaft would clear the face by only ~2 mm, into a cap that
is 14 mm deep. So it rides on four standoffs at `KNOB_PCB_Z = 8.5`.

The board is positioned **from the shaft tip down** (`KNOB_TIP_Z = 30.0`, giving 8 mm of engagement
into the cap) rather than from the floor up, because `KNOB_BODY_H` is the only estimate in that
chain. Measure "shaft tip above the Modulino PCB" once, put it in `KNOB_STACK_H`, and the standoffs
correct themselves.

Standoffs are on the datasheet's **32 × 16 mm** pattern, centred on the dial. The 41 mm board fits
the 45.6 mm column with 2.3 mm either side.

## Not yet modelled

The **buttons have holes but no internals** — switch bodies are still unselected, so there are no
mounts or plungers. The **I2C GPIO breakout** for the button wires is also unchosen; check its
address against 0x5D (touch), 0x2F (unidentified) and 0x76 (the Knob).

`BTN_PITCH` is currently 22 mm (Ø13 caps + a 9 mm gap) — note the design token `--btn-gap: 9mm` is
labelled "centre-to-centre", which is geometrically impossible with Ø13 caps; it has been read as
the gap.

No **light-pipe** for the dial's RGB ring; the design system lists one as an open question and the
Modulino Knob has no LEDs.

Also unresolved: whether the case exposes **BOOT/RESET** (front-x ≈ 172.6, right against the column,
so a hidden port through the column cavity is plausible) and **microSD** (front-x ≈ 159.7).
