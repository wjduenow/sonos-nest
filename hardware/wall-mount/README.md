# Sonos Knob — Wall Mount (twist-lock)

A two-part wall mount for the **Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display**
(DHE03921D). Self-contained Python CSG (`trimesh` + `manifold3d`) — no OpenSCAD
or display required.

```
   wall                         cradle (twists onto plate)
   │  ┌───────────┐             ┌──────────────┐
   │  │ disc      │             │  ╱        ╲   │  ← Ø58 cup holds display body
   │██│   ┌─────┐ │   bayonet   │ │  center   │ │  ← web: 3×Ø4 screws (Ø12 BC)
   │  │   │ring │◄├─────────────┤ │  web      │ │     into the display's back hub
   │  │   │+lugs│ │  twist-lock │  ╲________╱   │
   │  └───────────┘             ├──────────────┤
   └─ screws to wall             └─ skirt + J-slots
```

## How it secures (the two jobs)

1. **Plate → wall:** countersunk holes on a bolt circle **inside the bayonet
   ring** (driven before the cradle goes on); flat-head screws into anchors/stud,
   heads flush and hidden. A center hole + edge notch route the USB-C cable.
2. **Unit → plate:** a **bayonet twist-lock**. The plate's ring + 3 lugs sit
   inside the cradle's skirt; push on so the lugs enter the axial slots, then
   **twist ~25° to lock**. The display itself fastens to the cradle's center
   web with **3 small screws** into its rear hub — nothing touches the rotating
   front bezel.

## Where the cradle geometry comes from

The display-side of the cradle (the body-locating ring, center web, and the
**3 × Ø4 screw holes on a Ø12 mm bolt circle**) is taken directly from
`reference_mount.stl` — a known-good community mount for this exact display. The
build script **trims off the reference's own mounting base** (the back floor /
lower shell below the screw web — redundant now that our bayonet skirt does that
job; controlled by `REF_CUT`) and grafts only the display-holding top onto our
skirt. The proven display fit is preserved; the redundant middle layer is gone.

## Files

| File | Role |
|---|---|
| `bayonet_params.py` | **Shared** bayonet dims (standoff lives here). Imported by both builders so the ring/lugs and skirt/slots can't drift apart. |
| `build_wall_plate.py` | Builds `wall_plate.stl` |
| `build_cradle_from_reference.py` | Builds `cradle.stl` (grafts onto `reference_mount.stl`) |
| `build_all.py` | Runs both builders |
| `reference_mount.stl` | The community reference mount (source of the screw pattern) |
| `wall_plate.stl`, `cradle.stl` | The printable parts |
| `reference_holes.png` | Analysis of the reference's hole pattern |

## Build / regenerate

```bash
pip install --user trimesh manifold3d numpy shapely scipy rtree
cd hardware/wall-mount
python3 build_all.py          # -> wall_plate.stl + cradle.stl
```

Both parts come out **watertight**. Current sizes: plate **Ø61 × 12 mm**,
cradle **Ø62 × 30.2 mm** — sized to the display's rear body (the Ø79 front bezel
overhangs and hides the mount). Resize everything via `mount_od` in
`bayonet_params.py`.

### Tuning the standoff
Edit `ring_h` (and `lug_z`) in `bayonet_params.py`, then `python3 build_all.py`.
Smaller `ring_h` = closer to the wall; keep `ring_h` a couple mm above
`lug_z + lug_h/2` so the lug stays captured.

## Printing

> **Validation:** the 1:1 paper templates (`templates.pdf`) were checked against
> the physical unit — screw pattern and diameters line up. The models are
> print-ready. Always print the templates at **100% / Actual Size** and confirm
> the 100 mm calibration bar before trusting a measurement.

**Settings (validated):**

| Setting | Value |
|---|---|
| Material | PETG or ASA (sits near warm electronics / wall). PLA can creep with heat. |
| Cradle orientation | **Skirt-down** — bayonet J-slots print support-free; center web may want light supports. |
| Plate orientation | Disc flat on the bed, ring up — no supports. |
| Perimeters / infill | 3+ perimeters, 30–40% infill (the lugs carry the load). |
| Layer height | 0.2 mm is fine; the bayonet tolerances assume ~0.4 mm nozzle. |
| Screws (display) | 3 × short **M3** into the rear hub (Ø4 holes are clearance; ~6–8 mm length to pass the ~3 mm web + bite the threads). |
| Screws (wall) | 2 × ~#8 / M4 into anchors or a stud (heads countersink flush, inside the ring). |

- **Print the cradle first** as a single first-article: confirm the display
  seats, the 3 screws thread into the hub, and the twist-lock engages — *before*
  committing to the plate.
- **Fit tuning** (`bayonet_params.py`): cradle won't twist on → loosen
  `ring_fit`; twist too loose → lower `slot_w` or raise `lug_protrude`.

## Assembly

1. Screw the **plate** to the wall (level the screw pair). Feed USB-C through
   the center hole.
2. Fasten the **display** to the cradle's center web with 3 short screws into
   its rear hub.
3. Offer the cradle to the plate so the lugs meet the axial slots, push, and
   **twist to lock**. A dab of hot glue at one lug makes it twist-proof.

## Validation status
- [x] **Screw pattern** — paper template lined up with the unit's rear holes.
- [x] **Diameters** — Ø58 body / Ø79 bezel confirmed on the paper template.
- [x] **USB-C routing** — port is on the **back**; the Ø11 center pass-through
  + open back handle the cable. No side cutout needed.
- [ ] **First-article print** of the cradle to confirm the display seats and the
  screws thread before printing the plate.
