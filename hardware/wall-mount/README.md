# Sonos Knob — Wall Mount (twist-lock)

A two-part wall mount for the **Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display**
(DHE03921D). Parametric OpenSCAD — `sonos_knob_wall_mount.scad`.

```
   wall                    cradle (twists onto ring)
   │  ┌──────────┐         ┌────────────────┐
   │  │ disc     │         │ ╔════════════╗ │ ← front lip (retains device)
   │  │   ┌────┐ │         │ ║  DEVICE    ║ │
   │██│   │ring│◄├─────────┤ ║  (screen   ║ │
   │  │   │lugs│ │ bayonet │ ║   faces    ║ │
   │  │   └────┘ │         │ ║   out)     ║ │
   │  └──────────┘         │ ╚════════════╝ │
   └─ screws into wall      └────────────────┘
```

## How it secures (your two requirements)

1. **Plate → wall:** `screw_count` countersunk holes on a `screw_bc` bolt circle.
   Flat-head screws into drywall anchors or a stud; heads sit flush and are
   hidden behind the device. A center `cable_hole` (+ optional edge `side_notch`)
   routes the USB-C cable into the wall or along its surface.
2. **Unit → plate:** a **bayonet twist-lock**. The plate's ring + `lug_count`
   lugs sit behind the device; push the cradle on so the lugs enter the axial
   slots, then **twist ~25°** to lock. The device is captured between the
   cradle's front lip and the ring face — no screws into the device, and no
   reliance on its (undocumented) rear hole pattern.

## ⚠️ Measure before you print

Elecrow publishes **only** a `79 × 79 × 30 mm` envelope. Everything else is a
default. With calipers, confirm and edit these in the `.scad`:

| Variable | What to measure |
|---|---|
| `device_dia` | Outer diameter across the **back** of the body |
| `device_depth` | Front face → back face |
| `usb_angle_deg`, `usb_from_back` | Where the USB-C port sits (angle + height above back) |
| `usb_w`, `usb_h` | Port opening — leave slack for the **plug + cable boot** |
| `front_rim_static` | **Does the outer front rim rotate with the knob?** |

### The one real gotcha — the rotating front
On these "rotary" displays the input is often the front bezel/ring itself. If
the **entire front rim rotates**, a full-width retaining lip will rub and fight
the knob. Mitigations:
- Keep `lip_overlap` small (1–1.5 mm) so the lip only catches the static
  outermost edge, **or**
- Confirm there's a thin non-rotating outer rim and size `lip_overlap` to it.

If only a smaller center knob rotates (front rim static), the default 3 mm lip
is fine. **Check this first — it's the thing most likely to bite.**

## Printing

- **Render & export:** open in OpenSCAD, set `part = "plate"`, F6, export STL;
  repeat with `part = "cradle"`. (`part = "both"` is preview only.)
- **Material:** PETG or ASA preferred — it may sit near warm electronics / a
  sunny wall. PLA works but can creep/sag with heat.
- **Cradle orientation:** print **front-lip-down** (open back up). The bayonet
  slots then need no supports; the lip is a clean bottom surface.
- **Plate orientation:** disc flat on the bed, ring up. No supports.
- **Walls/infill:** 3+ perimeters, 30–40% infill — the lugs take the load.
- **Tuning fit:** if the cradle won't slide over the ring, raise `ring_fit`;
  if the device rattles, lower `clearance`; if the twist is too loose, lower
  `slot_w` or increase `lug_protrude`.

## Assembly

1. Screw the **plate** to the wall (level the screw pair). Feed USB-C through
   the center hole.
2. Drop the **device** into the cradle from the back, screen facing the lip;
   align the USB-C cutout.
3. Plug in USB-C. Offer the cradle to the plate so the lugs meet the axial
   slots, push, and **twist to lock**. Add a dab of hot glue or a set screw at
   one lug if you want it twist-proof.

## Status / open items
- [ ] Confirm `device_dia` / `device_depth` against the physical unit.
- [ ] Confirm whether the front rim rotates (sets `lip_overlap`).
- [ ] Confirm USB-C port location + plug clearance.
- [ ] First-article print of the cradle to dial in `clearance` / `ring_fit`.
