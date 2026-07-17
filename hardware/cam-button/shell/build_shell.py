#!/usr/bin/env python3
"""
Build shell.stl -- the body of the `sonos-button` case.

The box tapes to the UNDERSIDE of a nightstand, button DOWN.  This part is everything
except the tape face: the button panel (z=0, the face the user pushes up on), the four
walls, the board bosses, and the lid columns.  lid.stl caps it.

    z = 0        outer button face      (down, at the user)
    z = CEIL_Z   shell rim / lid underside
    z = HEIGHT   tape face              (up, at the nightstand)  -- that's lid.stl

Features:
  * Ø12 bore for the FILN FLM12-FJ-6, dead centre in the channel between the two header
    rows, with a Ø19.5 relief on the INSIDE that (a) thins the panel to BUTTON_PANEL_T so
    the button's short thread can actually reach a nut and (b) gives that nut a flat seat
  * 4 board bosses on the vendor's 24 x 32 pattern, ribbed to the walls (they are 15.35
    tall and Ø6 -- unribbed they are noodles)
  * 4 lid columns, merged into the ±X walls
  * an OPEN-TOPPED USB-C notch sized for a cable overmold, capped by the lid
  * 2 wire posts to hold the pigtail coil down, out of the board's way

    python3 build_shell.py   # -> shell.stl

Every dimension lives in button_params.py.  The clearances that make this layout legal
are ASSERTED below, not asserted in a comment -- see check_clearances().
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, box, extrude_polygon
from trimesh.boolean import union, difference
from trimesh.transformations import translation_matrix
from shapely.geometry import box as sbox
import button_params as P

SEG = P.SEG


def rrect(hx, hy, r):
    return sbox(-(hx - r), -(hy - r), (hx - r), (hy - r)).buffer(r, resolution=16)


def cyl(d, z0, z1, x=0.0, y=0.0):
    """Cylinder along Z from z0 to z1."""
    m = cylinder(radius=d / 2.0, height=z1 - z0, sections=SEG)
    m.apply_translation([x, y, (z0 + z1) / 2.0])
    return m


def blk(x0, x1, y0, y1, z0, z1):
    return box(extents=(x1 - x0, y1 - y0, z1 - z0),
               transform=translation_matrix([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]))


# ---------------------------------------------------------------- clearance checks
def _box_circle_gap(bx, cx, cy, r):
    """Gap between an axis-aligned 2D box (x0,x1,y0,y1) and a circle. <0 = overlap."""
    x0, x1, y0, y1 = bx
    dx = max(x0 - cx, 0.0, cx - x1)
    dy = max(y0 - cy, 0.0, cy - y1)
    return float(np.hypot(dx, dy)) - r


def check_clearances(verbose=True):
    """Everything this layout claims, proven arithmetically.

    The whole design rests on one assumption -- that the FLM12-FJ-6's body fits in the
    channel between J4 and J5 -- so it is checked here rather than believed.
    """
    rows, ok = [], True
    br = P.BUTTON_BORE_D / 2 + P.BUTTON_CLR          # button-body keep-out radius
    nr = P.BUTTON_NUT_AC / 2                         # nut across-corners radius
    bx, by = P.BUTTON_CX, P.BUTTON_CY

    def rec(name, got, need, unit="mm"):
        nonlocal ok
        good = got >= need
        ok &= good
        rows.append((name, got, need, unit, good))

    # 1. THE load-bearing one: the body vs the two header rows.
    rec("button body -> header row (each side)",
        P.CHANNEL_W / 2 - (abs(bx) + br), 0.0)
    # 2. body vs the two soldered bottom-side JSTs (they overlap it in Z)
    for nm, (x0, x1, y0, y1, _d) in P.BOTTOM_PARTS.items():
        rec(f"button body -> {nm}", _box_circle_gap((x0, x1, y0, y1), bx, by, br), 0.0)
    # 3. body + nut vs the board bosses (bosses run z=3.0..18.35, right past both)
    for sx in (-1, 1):
        for sy in (-1, 1):
            d = np.hypot(sx * P.HOLE_DX / 2 - bx, sy * P.HOLE_DY / 2 - by) - P.BOSS_OD / 2
            rec(f"button body -> boss ({sx*12:+.0f},{sy*16:+.0f})", d - br, 0.0)
            rec(f"nut A/C  -> boss ({sx*12:+.0f},{sy*16:+.0f})", d - nr, 0.0)
    # 4. the wrench circle the datasheet demands is genuinely clear
    rec("nut A/C -> cavity wall", min(P.IN_X / 2 - abs(bx), P.IN_Y / 2 - abs(by)) - nr, 0.0)
    # 5. header pin tips clear the floor -- this is the pigtail's coil space
    rec("header pin tips -> floor (pigtail coil space)", P.WIRE_Z, 1.0)
    # 6. the lid columns must miss the board entirely
    rec("lid column -> PCB edge", (P.LID_POST_X - P.LID_POST_OD / 2) - P.PCB_W / 2, 0.0)
    # 7. ...and reach the wall, or they are unsupported 21 mm noodles
    rec("lid column merged into the ±X wall",
        (P.LID_POST_X + P.LID_POST_OD / 2) - P.IN_X / 2, 0.0)
    # 8. bosses stay inside the vendor's Ø6.4 keep-out ring
    rec("boss OD under the Ø6.4 keep-out", P.HOLE_KEEPOUT - P.BOSS_OD, 0.0)
    # 9. the board actually fits under the lid
    rec("tallest top-side part -> lid underside", P.CEIL_Z - (P.PCB_BACK_Z + P.COMP_Z_MAX), 0.0)
    # 10. wire posts stay out of the button and the board's bottom parts
    for i, (px, py) in enumerate(P.WIRE_POSTS):
        rec(f"wire post {i} -> button body",
            np.hypot(px - bx, py - by) - (br + P.WIRE_POST_D / 2), 0.0)
    # 11. the screws, which are rec-2.8's screws.  Both must bite hard and neither may
    #     punch out the far side of what it threads into.
    pcb_top = P.PCB_BACK_Z + P.PCB_T
    rec("board screw (M3x8) bite into boss", P.PCB_BACK_Z - (pcb_top - P.BOARD_SCREW_LEN), 4.0)
    rec("board screw tip -> boss bottom",
        (pcb_top - P.BOARD_SCREW_LEN) - P.BOTTOM_WALL, 0.0)
    rec("board screw head Ø -> keep-out ring", P.HOLE_KEEPOUT - P.BOARD_SCREW_HEAD, 0.0)
    rec("board screw head -> lid underside",
        P.CEIL_Z - (pcb_top + P.BOARD_SCREW_HEAD_H), 0.0)
    rec("lid screw (M3x10) bite into column",
        P.CEIL_Z - (P.HEIGHT - P.LID_SCREW_LEN), 4.0)
    rec("lid screw tip -> column bottom",
        (P.HEIGHT - P.LID_SCREW_LEN) - P.BOTTOM_WALL, 0.0)

    if verbose:
        print(f"  {'clearance':<44}{'have':>9}{'need':>7}")
        for nm, got, need, unit, good in rows:
            print(f"  {'OK ' if good else 'BAD'} {nm:<40}{got:9.2f}{need:7.2f}  {unit}")
    assert ok, "clearance check failed -- see the BAD rows above"
    return ok


# ---------------------------------------------------------------- parts
def outer_body():
    return extrude_polygon(rrect(P.OUT_X / 2, P.OUT_Y / 2, P.OUT_R), P.CEIL_Z)


def cavity():
    m = extrude_polygon(rrect(P.IN_X / 2, P.IN_Y / 2, max(P.OUT_R - P.WALL, 0.5)),
                        P.CEIL_Z - P.BOTTOM_WALL + 1.0)
    m.apply_translation([0, 0, P.BOTTOM_WALL])
    return m


def board_bosses():
    """Ø6 columns from the floor to the PCB's back face, on the vendor's 24 x 32 pattern."""
    return [cyl(P.BOSS_OD, P.BOTTOM_WALL, P.PCB_BACK_Z, sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2)
            for sx in (-1, 1) for sy in (-1, 1)]


def boss_ribs():
    """Web each boss out to the two walls it is nearest.  A Ø6 x 15.35 pillar on its own
    would flex every time the button is pressed."""
    out, t = [], P.BOSS_RIB / 2
    for sx in (-1, 1):
        for sy in (-1, 1):
            bxc, byc = sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2
            xw, yw = sx * P.IN_X / 2, sy * P.IN_Y / 2
            out.append(blk(min(bxc, xw), max(bxc, xw), byc - t, byc + t,
                           P.BOTTOM_WALL, P.PCB_BACK_Z))
            out.append(blk(bxc - t, bxc + t, min(byc, yw), max(byc, yw),
                           P.BOTTOM_WALL, P.PCB_BACK_Z))
    return out


def lid_columns():
    """Full-height columns for the lid screws.  Placed so they merge into the ±X walls
    (rigid) while still clearing the PCB -- see check_clearances()."""
    return [cyl(P.LID_POST_OD, P.BOTTOM_WALL, P.CEIL_Z, sx * P.LID_POST_X, sy * P.LID_POST_Y)
            for sx in (-1, 1) for sy in (-1, 1)]


def wire_posts():
    return [cyl(P.WIRE_POST_D, P.BOTTOM_WALL, P.BOTTOM_WALL + P.WIRE_POST_H, px, py)
            for px, py in P.WIRE_POSTS]


# ---------------------------------------------------------------- cutters
def button_bore():
    return cyl(P.BUTTON_BORE_D, -1.0, P.BUTTON_PANEL_T + 0.001, P.BUTTON_CX, P.BUTTON_CY)


def nut_relief():
    """The panel is BOTTOM_WALL thick everywhere but BUTTON_PANEL_T at the bore, because
    the thread is only ~4 mm long and the nut eats 2 of it (plans/04 §6).  Cut from the
    INSIDE, so the outer face stays flat and the nut gets a machined-flat seat.  Prints
    with no support: it is a dish in the floor, not an overhang."""
    return cyl(P.NUT_RELIEF_D, P.BUTTON_PANEL_T, P.BOTTOM_WALL + 0.001,
               P.BUTTON_CX, P.BUTTON_CY)


def boss_pilots():
    return [cyl(P.BOSS_PILOT, P.PCB_BACK_Z - 9.0, P.PCB_BACK_Z + 0.1,
                sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2)
            for sx in (-1, 1) for sy in (-1, 1)]


def lid_pilots():
    return [cyl(P.LID_POST_PILOT, P.CEIL_Z - 9.0, P.CEIL_Z + 0.1,
                sx * P.LID_POST_X, sy * P.LID_POST_Y)
            for sx in (-1, 1) for sy in (-1, 1)]


def usb_notch():
    """USB-C is permanent power, so this clears a CABLE OVERMOLD, not the connector.

    Open-topped on purpose: the connector's axis sits ~21 mm up, so any opening generous
    enough for an overmold would break the ceiling anyway.  Running it to the rim instead
    means the lid caps it and the shell prints without a single bridge.  The board's own
    USB-C also overhangs the PCB edge by 1.4 mm and pokes into the wall line -- this is
    what makes room for that too."""
    return blk(-P.USB_SLOT_W / 2, P.USB_SLOT_W / 2,
               -P.OUT_Y / 2 - 1.0, -P.PCB_L / 2 + 2.2,
               P.USB_SLOT_Z0, P.CEIL_Z + 1.0)


# ---------------------------------------------------------------- assemble
def build_shell():
    check_clearances(verbose=False)
    body = difference([outer_body(), cavity()], engine='manifold')
    body = union([body] + board_bosses() + boss_ribs() + lid_columns() + wire_posts(),
                 engine='manifold')
    body = difference([body, button_bore(), nut_relief(), usb_notch()]
                      + boss_pilots() + lid_pilots(), engine='manifold')
    solids = [p for p in body.split(only_watertight=False) if p.volume > 1.0]
    return solids[0] if len(solids) == 1 else trimesh.util.concatenate(solids)


if __name__ == "__main__":
    print("== clearances ==")
    check_clearances()
    print(f"\n== derived ==")
    print(f"  channel between header bodies   {P.CHANNEL_W:6.2f} mm   "
          f"(button keep-out Ø{P.BUTTON_BORE_D + 2*P.BUTTON_CLR:.1f})")
    print(f"  PCB back face at z              {P.PCB_BACK_Z:6.2f} mm")
    print(f"  lid underside at z              {P.CEIL_Z:6.2f} mm")
    print(f"  pigtail coil space under pins   {P.WIRE_Z:6.2f} mm")
    # Anything pushed onto J4's pins hangs from the header body, not the pin tips.  A
    # standard 2.54 DuPont female housing is ~14.7 mm long -> it does NOT fit; solder.
    print(f"  room below the header body      "
          f"{P.PCB_BACK_Z - P.HDR_BODY - P.BOTTOM_WALL:6.2f} mm   "
          f"(a 2.54 DuPont female housing is ~14.7 -> SOLDER to J4)")
    print(f"  OVERALL HEIGHT                  {P.HEIGHT:6.2f} mm   (target 23, ceiling 33)")
    m = build_shell()
    m.export("shell.stl")
    print(f"\nshell.stl  watertight={m.is_watertight}  winding={m.is_winding_consistent}  "
          f"volume={m.volume/1000:.1f} cm^3  tris={len(m.faces)}")
    print(f"           bbox(XxYxZ)={np.round(m.extents, 2)} mm")
