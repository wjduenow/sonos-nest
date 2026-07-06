# Sonos Knob — Wall Mount (magnetic pull-off)

A two-part wall mount for the **Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display**
(DHE03921D). Self-contained Python CSG (`trimesh` + `manifold3d`) — no OpenSCAD
or display required.

```
   wall                          cradle (lifts straight off)
   │  ┌──────────────┐           ┌──────────────┐
   │  │ disc + lip   │           │  ╱        ╲   │  ← cup holds the display body
   │██│  ┌────────┐  │  magnets  │ │  center   │ │  ← web: 3×Ø4 screws (Ø12 BC)
   │  │  │ ◙    ◙ │◄─┼───────────┤ │  web      │ │     into the display's back hub
   │  │  └────────┘  │ pull-on/  │  ╲________╱   │
   │  └──────────────┘  pull-off └──────────────┘
   └─ screws to wall    no twist  └─ magnet pads + 1 key tab on the rim
```

The cup's rim **nests into a shallow lip** on the wall plate (which centres it and
carries its weight), disc **magnets** snap the two faces together, and a single
**key tab** drops into a slot in the lip so the screen stays upright. You **pull
straight off** to reboot/service the unit and **push back on** to reseat — no
twisting, which matters because the Ø79 front bezel rotates freely and you can't
grip the hidden body to twist a bayonet.

## How it secures (the two jobs)

1. **Plate → wall:** countersunk holes on a bolt circle **inside the lip** (driven
   before the cradle goes on); flat-head screws into anchors/stud, heads flush and
   hidden. A center hole + edge notch route the MX1.25/USB cable.
2. **Unit → plate:** **magnets + nest + key.** The cup rim drops into the plate's
   lip (centring + shear so it can't slide down), 4 disc magnets pull the faces
   together, and the cup's key tab seats in the lip's slot for orientation /
   anti-rotation. The display itself fastens to the cradle's center web with **3
   small screws** into its rear hub — nothing touches the rotating front bezel.

## Where the cradle geometry comes from

The display-side of the cradle (the body-locating cup, center web, and the **3 ×
Ø4 screw holes on a Ø12 mm bolt circle**) is taken directly from
`reference_mount.stl` — a known-good community mount for this exact display. The
build script **trims off the reference's own mounting base** (the back floor below
the screw web; controlled by `SCREW_HEAD_GAP`, keeping only a few mm behind the
web for the installed screw heads), then adds the magnetic interface:

- **Deeper board pocket:** the cup's barrel wall is extended `cup_depth_extra`
  (default **2 mm**) at the opening so the display seats deeper.
- **Magnet pads:** the wall-facing rim is only ~2 mm wide, so four local bosses
  **bulge inward** to host the Ø6 disc magnets without poking through the wall.
  They sit *below* the web, so the open back (cable + screw-head access) is kept.
- **Key tab:** one tab on the rim that drops into the wall-plate lip slot.

The proven display fit is preserved; the bayonet J-slots/skirt are gone.

## Files

| File | Role |
|---|---|
| `mount_params.py` | **Shared** interface dims (lip, magnets, key, cup depth). Imported by both builders so the plate and cradle can't drift apart. |
| `build_wall_plate.py` | Builds `wall_plate.stl` (disc + lip + magnet pockets + screws) |
| `build_cradle_from_reference.py` | Builds `cradle.stl` (grafts the magnetic rim onto `reference_mount.stl`) |
| `build_all.py` | Runs both builders |
| `render_preview.py` | Regenerates `assembly_preview.png` |
| `reference_mount.stl` | The community reference mount (source of the screw pattern) |
| `wall_plate.stl`, `cradle.stl` | The printable parts |

## Build / regenerate

```bash
pip install --user trimesh manifold3d numpy shapely scipy rtree matplotlib
cd hardware/round-nest-2.8/wall
python3 build_all.py          # -> wall_plate.stl + cradle.stl
python3 render_preview.py     # -> assembly_preview.png  (optional)
```

Both parts come out **watertight**. Current sizes: plate **Ø68.8 × 7 mm**, cradle
**Ø62 × 23.7 mm** — both still hidden behind the Ø79 front bezel. Tune the whole
interface from `mount_params.py`.

> A reload-into-`trimesh` of `wall_plate.stl` shows many "components" — that's a
> float32 STL vertex-merge artifact, not floating geometry. The authoritative
> watertight/volume check runs on the in-memory manifold (printed at build).

### Tuning
- **Pocket depth:** `cup_depth_extra` in `mount_params.py` (2 mm now). If the cup
  wall starts to touch the rotating bezel, reduce it.
- **Hold strength:** `magnet_d` / `magnet_count` / `magnet_bc`. Ø6×3 N52 ×4 is a
  good start; go Ø8×3 or 6 magnets for a firmer grip.
- **Key fit / anti-rotation:** `key_w_deg` vs `key_slot_deg` (slot is wider so the
  tab self-finds). Set `anti_rotation_key=False` to drop it entirely.
- **Nest fit:** `fit` (cup-to-lip gap, 0.4 mm). Looser if the cup binds in the lip.

## Printing

> **Validation:** the 1:1 paper templates (`templates.pdf`) were checked against
> the physical unit — screw pattern and diameters line up. Always print the
> templates at **100% / Actual Size** and confirm the 100 mm calibration bar.

**Settings:**

| Setting | Value |
|---|---|
| Material | PETG or ASA (sits near warm electronics / wall). PLA can creep with heat. |
| Cradle orientation | **Rim-down** — magnet pockets and key tab print support-free; center web may want light supports. |
| Plate orientation | Disc flat on the bed, lip up — no supports. |
| Perimeters / infill | 3+ perimeters, 30–40% infill. |
| Layer height | 0.2 mm. |
| Magnets | **8 × Ø6×3 mm N52 disc** (4 per part), glued in (CA/epoxy). Pockets are blind so faces sit ~flush. |
| Screws (display) | 3 × short **M3** into the rear hub (~6–8 mm). |
| Screws (wall) | 2 × ~#8 / M4 into anchors or a stud (heads countersink flush, inside the lip). |

**Magnet polarity — important:** glue **all wall-plate magnets one way** and **all
cradle magnets the other way**, so every pair attracts. Mark one pole with a
marker before gluing, and dry-stack a plate/cradle pair to confirm they pull
together (not repel) before the glue sets.

- **Print the cradle first** as a single first-article: confirm the display seats
  in the deeper pocket and the 3 screws thread into the hub — *before* the plate.

## Assembly

1. Screw the **plate** to the wall (level the screw pair). Feed the MX1.25/USB
   cable through the center hole.
2. Glue the 8 magnets (mind polarity), and fasten the **display** to the cradle's
   center web with 3 short screws into its rear hub.
3. Hold the cradle roughly upright so the key tab lines up with the lip slot, and
   let the magnets pull it home. To reboot/service: **pull it straight off**, then
   push it back on.

## Validation status
- [x] **Screw pattern** — paper template lined up with the unit's rear holes.
- [x] **Diameters** — Ø58 body / Ø79 bezel confirmed on the paper template.
- [x] **Cable routing** — rear port is a **4-pin MX1.25 JST**, on the back; the
  Ø11 center pass-through + open back clear the connector + cable.
- [x] **Digital fit-check** — seated interference 0 mm³; lip brackets the cup; key
  tab engages the slot; magnets meet at the mating plane (`render_preview.py`).
- [ ] **First-article print** of the cradle to confirm the deeper pocket seats the
  display and the screws thread before printing the plate.
- [ ] **Magnet pull** confirmed on printed parts (and bezel clearance with +2 mm).
