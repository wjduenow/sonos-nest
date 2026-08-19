#!/usr/bin/env python3
"""
Build shell.stl -- the body of the `button-v2` case.

The box tapes to the UNDERSIDE of a nightstand (or hangs on two screws through the lid's
keyhole slots), button DOWN.  This part is everything except the tape face: the button
panel (z=0, the face the user pushes up on), the four walls, the board retention, and the
lid columns.  lid.stl caps it.

    z = 0        outer button face      (down, at the user)
    z = CEIL_Z   shell rim / lid underside
    z = HEIGHT   tape face              (up, at the nightstand)  -- that's lid.stl

Features:
  * Ø12 bore for the FILN FLM12-FJ-6, dead centre, with a Ø19.5 relief on the INSIDE that
    (a) thins the panel to BUTTON_PANEL_T so the button's short thread can reach a nut and
    (b) gives that nut a flat seat
  * board retention with NO SCREWS -- the XIAO has no mounting holes.  Two X locating ribs,
    four corner ledges at PCB_BACK_Z, and (on the lid) a rib bearing on the RF shield can
  * 4 lid columns, merged into the ±X walls
  * an OPEN-TOPPED USB-C notch sized for a cable overmold, with pinch ribs and a zip-tie
    slot -- the strain relief that stands in for the mounting holes this board lacks
  * a notch in the +Y wall for the u.FL pigtail to drop into the antenna pocket

    python3 build_shell.py   # -> shell.stl

Every dimension lives in button_params.py.  The clearances that make this layout legal are
ASSERTED below, not asserted in a comment -- see check_clearances().
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
    """Axis-aligned box from two opposite corners, in either order.

    The sort is not decoration.  Half the features here are built with a `for sx in (-1, 1)`
    mirror, which flips the sign of both bounds and hands this a HIGHER low than high --
    trimesh then builds a box with a negative extent, which is inside-out rather than empty.
    It does not raise here; it raises several booleans later as "Not all meshes are volumes!",
    pointing at the union rather than at the part that made it."""
    x0, x1 = sorted((x0, x1))
    y0, y1 = sorted((y0, y1))
    z0, z1 = sorted((z0, z1))
    return box(extents=(x1 - x0, y1 - y0, z1 - z0),
               transform=translation_matrix([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]))


# ---------------------------------------------------------------- clearance checks
def check_clearances(verbose=True):
    """Everything this layout claims, proven arithmetically.

    Two claims carry the design and both are checked here rather than believed:
      * the board's LONG axis has to run along Y, or the Ø19.5 nut relief breaks the wall
      * the board's underside is flat, so ledges may sit anywhere under it
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

    # 1. THE load-bearing one: the nut relief has to fit the cavity's SHORT axis.  This is what
    #    forces the board's 20.96 side to run along Y -- rotate it and this goes negative.
    rec("nut relief Ø19.5 -> cavity short wall", P.IN_Y / 2 - abs(by) - P.NUT_RELIEF_D / 2, 0.0)
    rec("nut A/C wrench circle -> cavity wall",
        min(P.IN_X / 2 - abs(bx), P.IN_Y / 2 - abs(by)) - nr, 0.0)
    # 2. ...and the board must be no shorter than the relief in that axis, or the ledges would
    #    have to sit over the relief dish.
    rec("PCB long axis >= nut relief Ø", P.PCB_L - P.NUT_RELIEF_D, 0.0)

    # 3. Nothing on the board's underside -- from the STEP scan, not assumed.  This is what
    #    lets the ledges sit anywhere; the ESP32-S3-CAM had two soldered JSTs to dodge.
    rec("underside clear of obstructions", 1.0 if not P.BOTTOM_PARTS else -1.0, 1.0, "bool")

    # 4. The button body must clear the X locating ribs, which run past it in Z.
    rec("button body -> X locating rib", P.RIB_X - (abs(bx) + br), 0.0)

    # 5. THE ASSEMBLY CHECK, and the one that resized this box.  It is not enough for the NUT to
    #    fit -- the SOCKET has to reach it, or the nut is finger-tight and the button eventually
    #    rotates in its bore and tears its own wires off.  Both cavity axes, and the height at
    #    which the locating features start, are all measured against the tool.
    rec("16 mm socket -> cavity (short axis)", P.IN_Y - P.NUT_SOCKET_D, 2 * P.NUT_SOCKET_CLR)
    rec("16 mm socket -> cavity (long axis)", P.IN_X - P.NUT_SOCKET_D, 2 * P.NUT_SOCKET_CLR)
    rec("socket depth before the ribs start", P.NUT_ACCESS_Z - P.BOTTOM_WALL, 1.0)
    rec("nut top -> where the ribs start",
        P.NUT_ACCESS_Z - (P.BUTTON_PANEL_T + P.BUTTON_NUT_T), 0.5)

    # 6. The lid columns must miss the board entirely...
    rec("lid column -> PCB edge", (P.LID_POST_X - P.LID_POST_OD / 2) - P.PCB_W / 2, 0.0)
    # 7. ...and reach the wall, or they are unsupported 20 mm noodles.
    rec("lid column merged into the ±X wall",
        (P.LID_POST_X + P.LID_POST_OD / 2) - P.IN_X / 2, 0.0)
    # 8. The columns also must not foul the USB notch or the antenna pocket in Y.
    rec("lid column -> USB notch (in Y)",
        (P.LID_POST_Y - P.LID_POST_OD / 2) - (-P.IN_Y / 2 + 0.0), 0.0)

    # 9. The board fits under the lid, and the lid rib reaches the shield can without
    #    bottoming out on anything taller.
    rec("tallest top-side part -> lid underside", P.CEIL_Z - (P.PCB_BACK_Z + P.COMP_Z_MAX), 0.0)
    rec("lid rib projection (interference)", P.LID_RIB_H, 0.1)
    rec("lid rib stays inside the shield can (X)", (P.SHIELD_X1 - P.SHIELD_X0) - P.LID_RIB_W, 0.0)
    rec("lid rib stays inside the shield can (Y)", (P.SHIELD_Y1 - P.SHIELD_Y0) - P.LID_RIB_T, 0.0)
    # The rib must not reach the USB-C or the u.FL -- both are connectors, and the whole point
    # of bearing on the can is to keep retention load out of solder joints.
    rib_y0, rib_y1 = P.LID_RIB_Y - P.LID_RIB_T / 2, P.LID_RIB_Y + P.LID_RIB_T / 2
    rec("lid rib -> USB-C (in Y)", rib_y0 - P.USB_Y_INNER, 0.0)
    rec("lid rib -> u.FL (in Y)", P.UFL_Y0 - rib_y1, 0.0)

    # 10. Board ledges: they must be under the PCB but clear of the button's bore, and the
    #     board must actually land on them rather than on the relief dish.
    for sy in (-1, 1):
        ly = sy * P.LEDGE_Y
        d = np.hypot(max(abs(ly) - P.LEDGE_L / 2, 0.0), max(P.RIB_X - P.LEDGE_W - abs(bx), 0.0))
        rec(f"ledge (y={ly:+.1f}) -> button body", d - br, 0.0)
    rec("ledge reach under the PCB", P.PCB_W / 2 - (P.RIB_X - P.LEDGE_W), 0.0)

    # 11. Open space under the board for the antenna + harness.
    rec("clear height under the board", P.UNDER_BOARD_Z, 8.0)

    # 12. The USB notch has to clear the connector, which OVERHANGS the PCB edge by 1.52.
    rec("USB notch width -> connector", P.USB_SLOT_W - (P.USB_X1 - P.USB_X0), 0.0)
    rec("USB notch floor -> connector underside", (P.PCB_BACK_Z + P.USB_Z0) - P.USB_SLOT_Z0, 0.0)
    rec("USB connector overhang -> outer wall",
        P.OUT_Y / 2 - abs(P.USB_Y_EDGE), 0.0)
    # The strain-relief pinch must actually be narrower than the notch, or it grips nothing.
    rec("USB strain pinch narrower than the notch", P.USB_SLOT_W - P.USB_STRAIN_W, 1.0)

    # 13. The lid screws.  Same M3 self-tappers as cam-button and rec-2.8: each must bite hard
    #     and must not punch out the far side of what it threads into.
    rec("lid screw (M3x10) bite into column",
        P.CEIL_Z - (P.HEIGHT - P.LID_SCREW_LEN), 4.0)
    rec("lid screw tip -> column bottom",
        (P.HEIGHT - P.LID_SCREW_LEN) - P.BOTTOM_WALL, 0.0)

    # 14. Keyhole slots must miss the lid screws entirely, in the lid's own plane.
    if P.KEYHOLE_ON:
        for sy in (-1, 1):
            ky = sy * P.KEYHOLE_Y
            dx = abs(P.LID_POST_X) - (abs(P.KEYHOLE_X) + P.KEYHOLE_TRAVEL / 2 + P.KEYHOLE_HEAD_D / 2)
            dy = abs(abs(ky) - P.LID_POST_Y)
            rec(f"keyhole (y={ky:+.1f}) -> lid screw",
                float(np.hypot(max(dx, 0.0), dy)) - P.LID_SCREW_HEAD / 2, 0.0)
        rec("keyhole -> lid edge",
            P.OUT_Y / 2 - (P.KEYHOLE_Y + P.KEYHOLE_HEAD_D / 2) - 1.5, 0.0)

    if verbose:
        print(f"  {'clearance':<46}{'have':>9}{'need':>7}")
        for nm, got, need, unit, good in rows:
            print(f"  {'OK ' if good else 'BAD'} {nm:<42}{got:9.2f}{need:7.2f}  {unit}")
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


def locating_ribs():
    """Two ribs running the length of the cavity at x = ±RIB_X, plus two end packers at
    y = ±Y_PACK_IN.  Together they are the pocket the board drops into: 0.4 mm loose all round.

    ALL FOUR START AT NUT_ACCESS_Z, NOT AT THE FLOOR.  Below that the cavity has to stay clear
    wall-to-wall so a 16 mm socket can reach the M12 nut -- see button_params.py.  Each one
    therefore bridges a ~20 mm span between two walls at its underside, which prints fine and is
    an internal face nobody sees."""
    top = P.PCB_BACK_Z + P.PCB_T + P.RIB_TOP_Z
    ribs = [blk(sx * P.RIB_X, sx * (P.RIB_X + P.RIB_T), -P.IN_Y / 2, P.IN_Y / 2,
                P.NUT_ACCESS_Z, top)
            for sx in (-1, 1)]
    # End packers: the cavity is now wider than the board in Y too (the socket set it), so the
    # end walls no longer touch the board and have to be brought in to meet it.
    packers = [blk(-P.IN_X / 2, P.IN_X / 2, sy * P.Y_PACK_IN, sy * P.IN_Y / 2,
                   P.NUT_ACCESS_Z, top)
               for sy in (-1, 1)]
    return ribs + packers


def board_ledges():
    """Four pads at PCB_BACK_Z that the board simply drops onto.

    This is the whole board mount -- the XIAO has no mounting holes.  They reach LEDGE_W in
    from the ribs, i.e. only ~1.5 mm under the board's edge, which is why the flat underside
    verified in the STEP matters: there is nothing to clear beneath the rest of it."""
    out = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x0, x1 = sorted((sx * P.RIB_X, sx * (P.RIB_X - P.LEDGE_W)))
            out.append(blk(x0, x1,
                           sy * P.LEDGE_Y - P.LEDGE_L / 2, sy * P.LEDGE_Y + P.LEDGE_L / 2,
                           P.PCB_BACK_Z - 2.0, P.PCB_BACK_Z))
    return out


def lid_columns():
    """Full-height columns for the lid screws.  Placed so they merge into the ±X walls
    (rigid) while still clearing the PCB -- see check_clearances()."""
    return [cyl(P.LID_POST_OD, P.BOTTOM_WALL, P.CEIL_Z, sx * P.LID_POST_X, sy * P.LID_POST_Y)
            for sx in (-1, 1) for sy in (-1, 1)]


def usb_strain_ribs():
    """Two soft ribs that pinch the cable overmold inside the USB notch.

    WITHOUT MOUNTING HOLES THIS IS LOAD-BEARING.  Every gram of cable-insertion force that
    these do not absorb goes into the USB-C connector's solder joints, and there are no screws
    anywhere on this board to take it instead."""
    y0 = -P.OUT_Y / 2
    y1 = y0 + P.WALL
    return [blk(sx * P.USB_STRAIN_W / 2, sx * (P.USB_STRAIN_W / 2 + P.USB_STRAIN_T),
                y0, y1, P.USB_SLOT_Z0, P.CEIL_Z)
            for sx in (-1, 1)]


# ---------------------------------------------------------------- cutters
def button_bore():
    return cyl(P.BUTTON_BORE_D, -1.0, P.BUTTON_PANEL_T + 0.001, P.BUTTON_CX, P.BUTTON_CY)


def nut_relief():
    """The panel is BOTTOM_WALL thick everywhere but BUTTON_PANEL_T at the bore, because the
    thread is only ~4 mm long and the nut eats 2 of it (plans/04 §6).  Cut from the INSIDE, so
    the outer face stays flat and the nut gets a machined-flat seat.  Prints with no support:
    it is a dish in the floor, not an overhang."""
    return cyl(P.NUT_RELIEF_D, P.BUTTON_PANEL_T, P.BOTTOM_WALL + 0.001,
               P.BUTTON_CX, P.BUTTON_CY)


def lid_pilots():
    return [cyl(P.LID_POST_PILOT, P.CEIL_Z - 9.0, P.CEIL_Z + 0.1,
                sx * P.LID_POST_X, sy * P.LID_POST_Y)
            for sx in (-1, 1) for sy in (-1, 1)]


def usb_notch():
    """USB-C is permanent power, so this clears a CABLE OVERMOLD, not the connector.

    Open-topped on purpose: the overmold's axis sits at PCB_BACK_Z + 2.36 = 20.71, so any
    opening generous enough would break the ceiling anyway.  Running it to the rim instead means
    the lid caps it and the shell prints without a single bridge.  The board's own USB-C also
    overhangs the PCB edge by 1.52 mm and pokes into the wall line -- this makes room for that
    too."""
    return blk(-P.USB_SLOT_W / 2, P.USB_SLOT_W / 2,
               -P.OUT_Y / 2 - 1.0, -P.PCB_L / 2 + 1.0,
               P.USB_SLOT_Z0, P.CEIL_Z + 1.0)


def tie_slot():
    """A zip-tie slot through the -Y wall beside the notch: belt and braces on the pinch ribs.
    Cheap, and it is the only strain relief that survives somebody using a fat cable that the
    ribs cannot grip."""
    return [blk(sx * (P.USB_SLOT_W / 2 + 1.5), sx * (P.USB_SLOT_W / 2 + 1.5 + P.TIE_SLOT_W),
                -P.OUT_Y / 2 - 1.0, -P.IN_Y / 2 + 0.5,
                P.CEIL_Z - P.TIE_SLOT_H - 0.5, P.CEIL_Z - 0.5)
            for sx in (-1, 1)]


def ufl_notch():
    """A gap in the +Y locating wall for the u.FL pigtail to drop through into the antenna
    pocket under the board.

    Cut across the FULL WIDTH deliberately: which side the u.FL ends up on depends on which way
    round the board is dropped in, and a notch that only works one way is a notch that will be
    wrong half the time."""
    w = P.IN_X if P.UFL_NOTCH_FULL_WIDTH else P.UFL_NOTCH_W
    return blk(-w / 2, w / 2,
               P.PCB_L / 2 - 0.5, P.IN_Y / 2 + P.UFL_NOTCH_D,
               P.PCB_BACK_Z - P.UFL_NOTCH_Z, P.PCB_BACK_Z + 0.001)


# ---------------------------------------------------------------- assemble
def build_shell():
    check_clearances(verbose=False)
    body = difference([outer_body(), cavity()], engine='manifold')
    body = union([body] + locating_ribs() + board_ledges() + lid_columns() + usb_strain_ribs(),
                 engine='manifold')
    body = difference([body, button_bore(), nut_relief(), usb_notch(), ufl_notch()]
                      + lid_pilots() + tie_slot(), engine='manifold')
    solids = [p for p in body.split(only_watertight=False) if p.volume > 1.0]
    return solids[0] if len(solids) == 1 else trimesh.util.concatenate(solids)


if __name__ == "__main__":
    print("== clearances ==")
    check_clearances()
    print(f"\n== derived ==")
    print(f"  PCB (STEP)                      {P.PCB_W:.2f} x {P.PCB_L:.2f} x {P.PCB_T:.2f} mm, "
          f"underside FLAT, no mounting holes")
    print(f"  PCB back face at z              {P.PCB_BACK_Z:6.2f} mm")
    print(f"  lid underside at z              {P.CEIL_Z:6.2f} mm")
    print(f"  clear space under the board     {P.UNDER_BOARD_Z:6.2f} mm   "
          f"(antenna + harness live here)")
    print(f"  lid rib projection              {P.LID_RIB_H:6.2f} mm   "
          f"(onto the shield can, ~0.2 interference)")
    print(f"  FOOTPRINT                       {P.OUT_X:6.2f} x {P.OUT_Y:.2f} mm")
    print(f"  OVERALL HEIGHT                  {P.HEIGHT:6.2f} mm")
    cam = 48.2 * 44.2 * 26.81 / 1000.0
    here = P.OUT_X * P.OUT_Y * P.HEIGHT / 1000.0
    print(f"  bounding volume                 {here:6.2f} cm^3  "
          f"vs cam-button {cam:.1f} -> {cam/here:.2f}x smaller")
    m = build_shell()
    m.export("shell.stl")
    print(f"\nshell.stl  watertight={m.is_watertight}  winding={m.is_winding_consistent}  "
          f"volume={m.volume/1000:.1f} cm^3  tris={len(m.faces)}")
    print(f"           bbox(XxYxZ)={np.round(m.extents, 2)} mm")
