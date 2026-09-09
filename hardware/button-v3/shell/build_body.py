"""button-v3 body — four walls, a CLOSED rear, and the posts the board rests on.

The rear is closed and the FRONT is open: the board loads from the front, past the removed bezel.
That one reversal is what let the posts become body-integral and removed both the carrier plate
and every screw into the board — see button_params.py section 5.

Every dimension comes from button_params.py; nothing is typed twice. The clearances that matter
are ASSERTED in check_clearances(), which build_body() runs BEFORE exporting, so a violating STL
cannot be written.

Datum (button_params.py has the full note):
    z = 0 outer TOP face, +z DOWN      y = 0 outer FRONT face, +y BACK      x = 0 width centreline
"""
import numpy as np
import trimesh
from shapely.geometry import box as sbox
import button_params as P

SEG = P.SEG
ENG = "manifold"


def rrect_prism(hx, hy, r, z0, z1, cx=0.0, cy=0.0):
    """Rounded rectangle in X-Y, extruded along Z."""
    poly = sbox(-hx, -hy, hx, hy).buffer(-r, resolution=SEG // 4).buffer(r, resolution=SEG // 4)
    m = trimesh.creation.extrude_polygon(poly, height=z1 - z0)
    m.apply_translation((cx, cy, z0))
    return m


def blk(x0, x1, y0, y1, z0, z1):
    m = trimesh.creation.box(extents=(x1 - x0, y1 - y0, z1 - z0))
    m.apply_translation(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    return m


def cyl_y(d, y0, y1, x=0.0, z=0.0):
    """Cylinder along Y (the depth axis) — lid posts and board pillars."""
    m = trimesh.creation.cylinder(radius=d / 2, height=y1 - y0, sections=SEG)
    m.apply_transform(trimesh.transformations.rotation_matrix(-np.pi / 2, (1, 0, 0)))
    m.apply_translation((x, (y0 + y1) / 2, z))
    return m


def cyl_z(d, z0, z1, x=0.0, y=0.0):
    m = trimesh.creation.cylinder(radius=d / 2, height=z1 - z0, sections=SEG)
    m.apply_translation((x, y, (z0 + z1) / 2))
    return m


# ---------------------------------------------------------------- clearance checks
def check_clearances(verbose=True):
    rows, ok = [], True

    def rec(name, got, need, unit="mm"):
        nonlocal ok
        good = got >= need
        ok &= good
        rows.append((name, got, need, unit, good))

    # 1. The carrier post that nearly did not fit. At x = -16.215 a 5.0 mm post would bury itself
    #    in the cavity wall; 4.5 clears by 0.12. Tightest thing in the assembly, and the reason
    #    CARRIER_POST_OD is not a round number.
    rec("cavity wall clear of board post",
        P.IN_X / 2 - (max(abs(P.HOLE_XS[0]), P.HOLE_XS[1]) + P.POST_OD / 2), 0.05)

    # 1d. Growing the box to centre the screen must not leave the board short of floor.
    rec("height covers the board", P.HEIGHT - P.HEIGHT_MIN, 0.0)

    # 1e. The lit area is what gets centred, not the board. Prove it landed.
    rec("lit area centred in X", 0.05 - abs(P.BOARD_X0 + P.DISP_CX), 0.0)
    rec("lit area centred in Z", 0.05 - abs((P.PCB_TOP_Z + P.DISP_CZ) - P.HEIGHT / 2), 0.0)

    # 1f. Upper lid posts vs the M12 nut's swept circle.
    rec("upper bezel boss clear of the nut",
        (P.BOSS_X - P.BOSS_OD / 2) - P.BUTTON_NUT_AC / 2, 0.5)

    # 1b. The carrier itself must clear the M12 nut, which sweeps the full cavity depth at the top
    #     of the box — the reason the plate stops at the board's top edge instead of running to
    #     the cavity roof and giving the spigot a full-perimeter seat.
    rec("board top edge below the M12 nut",
        P.PCB_TOP_Z - (P.BUTTON_PANEL_T + P.BUTTON_NUT_T), 1.0)

    # 1c. The bezel's lip must press only on DEAD glass. If it ever reached the lit area it would
    #     be clamping the picture, which is both ugly and the one failure a reprint cannot fix.
    rec("bezel lip on dead glass, right", P.BOARD_X1 - P.WIN_X1, 0.8)
    rec("bezel lip on dead glass, bottom", (P.PCB_TOP_Z + P.PCB_H) - P.WIN_Z1, 0.8)
    rec("bezel lip clear of lit area, right",
        P.WIN_X1 - (P.BOARD_X0 + P.DISP_OFF_X + P.DISP_ACT_W), 0.0)
    rec("bezel lip clear of lit area, bottom",
        P.WIN_Z1 - (P.PCB_TOP_Z + P.DISP_OFF_Z + P.DISP_ACT_H), 0.0)

    # 2. The window must not cover a single lit pixel. Checked on all four sides independently,
    #    because the clamp to the board outline makes two of them behave differently from the
    #    other two.
    rec("window clears lit area, left",   (P.BOARD_X0 + P.DISP_OFF_X) - P.WIN_X0, 0.0)
    rec("window clears lit area, right",  P.WIN_X1 - (P.BOARD_X0 + P.DISP_OFF_X + P.DISP_ACT_W), 0.0)
    rec("window clears lit area, top",    (P.PCB_TOP_Z + P.DISP_OFF_Z) - P.WIN_Z0, 0.0)
    rec("window clears lit area, bottom", P.WIN_Z1 - (P.PCB_TOP_Z + P.DISP_OFF_Z + P.DISP_ACT_H), 0.0)

    # 3. ...and must not run off the board either, which would open a hairline into the case.
    rec("window inside board, left",   P.WIN_X0 - P.BOARD_X0, 0.0)
    rec("window inside board, right",  P.BOARD_X1 - P.WIN_X1, 0.0)
    rec("window inside board, top",    P.WIN_Z0 - P.PCB_TOP_Z, 0.0)
    rec("window inside board, bottom", (P.PCB_TOP_Z + P.PCB_H) - P.WIN_Z1, 0.0)

    # 4. The M12 nut is what sets the box depth; prove it still fits after everything else moved.
    rec("cavity depth for M12 nut", P.IN_Y - P.BUTTON_NUT_AC, 2 * P.NUT_SOCKET_CLR)
    rec("cavity width for M12 nut", P.IN_X - P.BUTTON_NUT_AC, 2 * P.NUT_SOCKET_CLR)

    # 5. The button body plus its solder tail must clear the board's top edge. This is the whole
    #    height budget of the box.
    rec("button tail to board top edge", P.PCB_TOP_Z - (P.BUTTON_INSIDE_T + P.BUTTON_TAIL_T),
        P.BUTTON_PCB_CLR)

    # 6. Electronics fit the cavity they are not setting.
    rec("cavity depth for the board stack", P.CAVITY_Y1 - P.BOARD_Y_REAR, P.STACK_REAR_CLR)
    rec("board bottom edge to cavity floor", (P.HEIGHT - P.WALL) - (P.PCB_TOP_Z + P.PCB_H), 0.0)


    # 8. USB notch actually covers the connector, with cable slop.
    rec("usb notch over connector, Z", P.USB_SLOT_W - P.USB_WIDTH, 2.0)
    rec("usb notch over connector, Y", P.USB_SLOT_Y1 - P.USB_SLOT_Y0, P.USB_HEIGHT + 2.0)

    # 9. The bezel bosses must sit clear of the board, in the bands above and below it.
    rec("upper boss above the board", P.PCB_TOP_Z - (P.BOSS_ZS[0] + P.BOSS_OD / 2), 0.5)
    rec("lower boss below the board", (P.BOSS_ZS[1] - P.BOSS_OD / 2) - (P.PCB_TOP_Z + P.PCB_H), 0.5)

    if verbose:
        print(f"  {'check':<38} {'got':>8} {'need':>8}")
        for name, got, need, unit, good in rows:
            print(f"  {name:<38} {got:8.2f} {need:8.2f} {unit}  {'ok' if good else '** BAD **'}")
    assert ok, "clearance check failed -- see the BAD rows above"
    return ok


# ---------------------------------------------------------------- parts
def full_outer():
    """The complete outer prism, front face to lid face.

    The shell and the lid are both CUT FROM THIS, rather than each being modelled to its own
    dimensions. That is the point: the two parts meet on a visible seam all the way round the box,
    and two independently-built rounded profiles would never quite agree there.
    """
    return rrect_prism(P.OUT_X / 2, P.OUT_Y / 2, P.OUT_R, 0.0, P.HEIGHT, cy=P.OUT_Y / 2)


def outer_body():
    return trimesh.boolean.intersection(
        [full_outer(), blk(-P.OUT_X, P.OUT_X, P.BODY_Y0, P.OUT_Y + 1.0, -1.0, P.HEIGHT + 1.0)],
        engine=ENG)


def board_posts():
    """The four the board rests on — at the eyelets, which are the one place with guaranteed
    component keepout on the back of the board.

    No bore: nothing screws into them. They exist to define a plane. A rigid board on four coplanar
    posts stays flat under a clamping force anywhere inside their footprint, which is what lets the
    bezel hold it with an L-shaped lip on only two edges.
    """
    return trimesh.boolean.union(
        [cyl_y(P.POST_OD, P.POST_Y1, P.CAVITY_Y1, x=x, z=z)
         for x in P.HOLE_XS for z in P.HOLE_ZS], engine=ENG)


def bezel_bosses():
    """What the bezel's four screws bite into.

    Tucked into the corners and overlapping each wall by BOSS_MERGE, so every boss is tied to two
    walls rather than standing alone. That matters more than usual: these four screws are the only
    thing holding the board down.

    ⚠️ The UPPER pair still has to dodge the M12 nut, whose swept circle is 19.48 across on the
    centreline — anything inside |x| = 9.74 up there lands in it.
    """
    return trimesh.boolean.union(
        [cyl_y(P.BOSS_OD, P.BODY_Y0, P.BODY_Y0 + P.BOSS_LEN, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def boss_pilots():
    return trimesh.boolean.union(
        [cyl_y(P.BOSS_PILOT, P.BODY_Y0 - 0.01, P.BODY_Y0 + P.BOSS_LEN + 0.01,
               x=sx * P.BOSS_X, z=z) for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def cavity():
    # Open at the FRONT (extends forward past the body so the boolean leaves no skin), closed at
    # the rear by BACK_WALL.
    y0, y1 = P.BODY_Y0 - 1.0, P.CAVITY_Y1
    return rrect_prism(P.IN_X / 2, (y1 - y0) / 2, max(P.OUT_R - P.WALL, 0.5),
                       P.WALL, P.HEIGHT - P.WALL, cy=(y0 + y1) / 2)


# ---------------------------------------------------------------- cutters
def button_bore():
    return cyl_z(P.BUTTON_BORE_D, -1.0, P.WALL + 1.0, x=P.BUTTON_CX, y=P.BUTTON_Y)


def nut_relief():
    """Thin the top wall to the button's panel thickness over the nut's swept circle."""
    return cyl_z(P.NUT_RELIEF_D, P.BUTTON_PANEL_T, P.WALL + 0.01, x=P.BUTTON_CX, y=P.BUTTON_Y)


def usb_notch():
    return blk(P.IN_X / 2 - 0.01, P.OUT_X / 2 + 1.0,
               P.USB_SLOT_Y0, P.USB_SLOT_Y1,
               P.USB_SLOT_ZC - P.USB_SLOT_W / 2, P.USB_SLOT_ZC + P.USB_SLOT_W / 2)


def build_body():
    check_clearances(verbose=False)

    m = outer_body()
    m = trimesh.boolean.difference([m, cavity()], engine=ENG)
    m = trimesh.boolean.union([m, board_posts(), bezel_bosses()], engine=ENG)
    for cutter in (nut_relief(), button_bore(), usb_notch(), boss_pilots()):
        m = trimesh.boolean.difference([m, cutter], engine=ENG)
    return m


if __name__ == "__main__":
    check_clearances(verbose=True)
    print()
    print("  == derived ==")
    print(f"  board front face z      {P.PCB_TOP_Z:.2f} .. {P.PCB_TOP_Z + P.PCB_H:.2f}")
    print(f"  board Y chain           {P.BOARD_Y_GLASS} glass / {P.BOARD_Y_PCB_BACK} pcb back /"
          f" {P.BOARD_Y_REAR} rear")
    print(f"  window                  {P.WIN_X1 - P.WIN_X0:.2f} x {P.WIN_Z1 - P.WIN_Z0:.2f}")
    print(f"  OVERALL                 {P.OUT_X:.2f} x {P.OUT_Y:.2f} x {P.HEIGHT:.2f} mm")
    print()
    m = build_body()
    m.export("body.stl")
    print(f"  body.stl   watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
    print(f"             bbox={np.round(m.bounds, 2).tolist()}")
