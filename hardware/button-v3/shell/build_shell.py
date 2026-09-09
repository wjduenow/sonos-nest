"""button-v3 shell — front wall + window, four side walls, open at the rear.

Every dimension comes from button_params.py; nothing is typed twice. The clearances that matter
are ASSERTED in check_clearances(), which build_shell() runs BEFORE exporting, so a violating STL
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

    # 1. The pillar that nearly did not fit. At x = +/-16.215 a 5.0 mm pillar would bury itself in
    #    the cavity wall; 4.5 clears by 0.12. This is the tightest thing in the whole part and the
    #    reason PILLAR_OD is not a round number.
    rec("cavity wall clear of pillar", P.IN_X / 2 - (abs(P.HOLE_XS[0]) + P.PILLAR_OD / 2), 0.05)

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
    rec("cavity depth for the board stack", (P.OUT_Y - P.LID_T) - P.BOARD_Y_REAR, P.STACK_REAR_CLR)
    rec("board bottom edge to cavity floor", (P.HEIGHT - P.WALL) - (P.PCB_TOP_Z + P.PCB_H), 0.0)

    # 7. The board must be able to LOAD from the rear. The board stop is the one shell feature that
    #    reaches into its footprint, so it has to stay above the window and out of the glass.
    rec("board stop above the window", P.WIN_Z0 - (P.PCB_TOP_Z - P.BOARD_STOP_Z), 0.0)

    # 8. USB notch actually covers the connector, with cable slop.
    rec("usb notch over connector, Z", P.USB_SLOT_W - P.USB_WIDTH, 2.0)
    rec("usb notch over connector, Y", P.USB_SLOT_Y1 - P.USB_SLOT_Y0, P.USB_HEIGHT + 2.0)

    # 9. Printability: the front wall left around the window on the two lipped sides.
    rec("front-wall lip, right",  P.BOARD_X1 - P.WIN_X1, 0.8)
    rec("front-wall lip, bottom", (P.PCB_TOP_Z + P.PCB_H) - P.WIN_Z1, 0.8)

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
        [full_outer(), blk(-P.OUT_X, P.OUT_X, -1.0, P.OUT_Y_SHELL, -1.0, P.HEIGHT + 1.0)],
        engine=ENG)


def lid_screw_posts():
    """Two columns the lid screws bite into.

    ⚠️ Their position is forced, and is the only place they could go. The board fills the cavity
    across almost its whole width (36.37 in a 37.17 opening), so nothing fits beside it; below it
    there is 0.4 mm; above it is the button. The M12 nut's swept circle is 19.48 across centred on
    x = 0, which leaves exactly two slivers at |x| > 9.74 in the button region — these sit there.
    """
    return trimesh.boolean.union(
        [cyl_y(P.LID_POST_OD, P.LID_POST_Y0, P.OUT_Y_SHELL, x=sx * P.LID_POST_X, z=P.LID_POST_Z)
         for sx in (-1, 1)], engine=ENG)


def lid_pilots():
    return trimesh.boolean.union(
        [cyl_y(P.LID_POST_PILOT, P.LID_POST_Y0 - 0.01, P.OUT_Y_SHELL + 1.0,
               x=sx * P.LID_POST_X, z=P.LID_POST_Z) for sx in (-1, 1)], engine=ENG)


def cavity():
    # Open at the rear: extends past the shell's back face so the boolean leaves no skin.
    return rrect_prism(P.IN_X / 2, (P.OUT_Y_SHELL - P.FRONT_WALL) / 2 + 1.0,
                       max(P.OUT_R - P.WALL, 0.5), P.WALL, P.HEIGHT - P.WALL,
                       cy=P.FRONT_WALL + (P.OUT_Y_SHELL - P.FRONT_WALL) / 2 + 1.0)


def board_stop():
    """A rib across the top of the board pocket so the board cannot ride up into the button."""
    return blk(-P.IN_X / 2, P.IN_X / 2,
               P.FRONT_WALL, P.BOARD_Y_PCB_BACK,
               P.PCB_TOP_Z - P.BOARD_STOP_Z, P.PCB_TOP_Z)


# ---------------------------------------------------------------- cutters
def window():
    return blk(P.WIN_X0, P.WIN_X1, -1.0, P.FRONT_WALL + 0.01, P.WIN_Z0, P.WIN_Z1)


def button_bore():
    return cyl_z(P.BUTTON_BORE_D, -1.0, P.WALL + 1.0, x=P.BUTTON_CX, y=P.BUTTON_Y)


def nut_relief():
    """Thin the top wall to the button's panel thickness over the nut's swept circle."""
    return cyl_z(P.NUT_RELIEF_D, P.BUTTON_PANEL_T, P.WALL + 0.01, x=P.BUTTON_CX, y=P.BUTTON_Y)


def usb_notch():
    return blk(P.IN_X / 2 - 0.01, P.OUT_X / 2 + 1.0,
               P.USB_SLOT_Y0, P.USB_SLOT_Y1,
               P.USB_SLOT_ZC - P.USB_SLOT_W / 2, P.USB_SLOT_ZC + P.USB_SLOT_W / 2)


def build_shell():
    check_clearances(verbose=False)

    m = outer_body()
    m = trimesh.boolean.difference([m, cavity()], engine=ENG)
    m = trimesh.boolean.union([m, board_stop(), lid_screw_posts()], engine=ENG)
    for cutter in (window(), nut_relief(), button_bore(), usb_notch(), lid_pilots()):
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
    m = build_shell()
    m.export("shell.stl")
    print(f"  shell.stl  watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
    print(f"             bbox={np.round(m.bounds, 2).tolist()}")
