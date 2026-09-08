# sonos-button-v3 shell

The `button-v3` case: a Waveshare ESP32-S3-LCD-1.47B behind a screen window, with a FILN
FLM12-FJ-6 illuminated button on top. Firmware and rationale: `plans/14-button-v3.md`.

> **Status: SCAFFOLD.** `shell/button_params.py` carries every number that could be derived or
> read off a vendor drawing. The geometry is not written yet, because seven dimensions have to
> come off a caliper first — §2. Two of the three outside dimensions are already fixed:
> **X = 42.17, Z = 39.22 mm.**

## 1. The idea

Same product as `hardware/button-v2`, plus a screen. Landscape: the board's long axis is
horizontal so the display reads landscape, matching `DISPLAY_ROTATION 1` as shipped.

Three things set the shape, and none of them is the PCB:

- **Depth is set by the M12 NUT, not the electronics.** The button tightens from inside, so the
  cavity has to swallow 18.48 mm across corners — more than the display + PCB + component stack
  needs. `hardware/button-v2` has the identical `max()` for the identical reason.
- **Height is set by the button sitting ABOVE the board.** Its body plus solder tail is 15.00 mm,
  and the board spans the full width so it cannot tuck under. `PCB_TOP_Z = 16.00` falls straight
  out of that.
- **Width is the only dimension the PCB wins**, at 36.37 + gaps + walls = 42.17.

**Retention is four M2 bosses off the front wall**, not `button-v2`'s ledges-and-lid-rib. A screen
has to stay square in its window and ledges do not control rotation. The rear is the lid.

## 2. Where every number came from

### From the vendor drawing (`ESP32-S3-LCD-1.47B-details-size.jpg`, read at 3x)

PCB **36.37 x 20.32**; header **2.54** pitch with rows **17.78** apart and the last pad **11.31**
from the right edge; mounting holes explicitly **M2**.

Waveshare publishes **no STEP model** for this board — checked. The only Drive link on the wiki is
the 363 MB Arduino installer.

> ⚠️ **17.78 is the header ROW SPACING, not a hole pitch**, and it reads like one. The arithmetic
> settles it: 17.78 + 2 x 1.27 = 20.32 exactly, i.e. two pad rows inset 1.27 mm from each long
> edge. Taken as a hole pitch it would have put the bottom holes 0.98 mm off the end of the board.

### ⚠️ NOT VERIFIED — measure these seven before anything is built

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
shell/button_params.py   single source of truth   <- only this exists so far
shell/build_shell.py     not written yet
shell/build_lid.py       not written yet
shell/build_all.py       not written yet
```

Built with Python CSG (trimesh + manifold3d), never OpenSCAD — see `hardware/README.md`:

```bash
conda run -n img23d python hardware/button-v3/shell/button_params.py   # print the derivations
conda run -n img23d python hardware/button-v3/shell/build_all.py       # once it exists
```

## 4. Still open

1. The seven measurements in §2.
2. Which side wall the USB-C exits, which decides whether the cable leaves left or right.
3. Whether the ring's 5 V low-side drive works off this board's header — unproven, and the last
   thing that could still change the BOM (`plans/14` §Open).
4. Strain relief for the USB-C cable. `button-v2` takes that load on pinch ribs because the XIAO
   has no mounting holes; with four M2 bosses here it may not be needed at all.
