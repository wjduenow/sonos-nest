#!/usr/bin/env python3
"""
Build shell.stl -- the rear body of the sonos-jukebox 7" flush wall case.

Features:
  * rounded outer body, hollowed to a 2.5 mm rear wall and 3 mm side walls
  * PCB pocket with 4 self-tap bosses on the 170.90 x 98.00 hole pattern
  * 2 KEYHOLE slots in the top band -- the whole wall mount, adding 0 mm of depth
  * USB-C breakout cradle at the bottom of the control column, with an oversized
    funnelled port through the rear wall so the wall's cable hole need not be exact
  * a channel across the rear for the 2-wire power run to J10 (far LEFT)
  * 4 face-plate screw bosses

    conda run -n img23d python build_shell.py     # -> shell.stl

Geometry lives in case_params.py.  Frame: +X right, +Y up, +Z toward the viewer,
z = 0 is the rear plane against the wall.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, box, extrude_polygon
from trimesh.boolean import union, difference
from shapely.geometry import box as sbox
from shapely.ops import unary_union
import case_params as P


def rrect(x0, y0, x1, y1, r):
    """Rounded rectangle polygon in the XY plane."""
    return sbox(x0 + r, y0 + r, x1 - r, y1 - r).buffer(r, resolution=24)


def prism(poly, z0, z1):
    m = extrude_polygon(poly, height=z1 - z0)
    m.apply_translation([0, 0, z0])
    return m


def cyl(r, z0, z1, x, y, seg=P.SEG):
    m = cylinder(radius=r, height=z1 - z0, sections=seg)
    m.apply_translation([x, y, (z0 + z1) / 2.0])
    return m


def bx(x0, y0, z0, x1, y1, z1):
    m = box(extents=[x1 - x0, y1 - y0, z1 - z0])
    m.apply_translation([(x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0])
    return m


def keyhole_poly(cx):
    """Keyhole outline: entry circle at the top, slot running down by KEY_DROP."""
    from shapely.geometry import Point
    entry = Point(cx, P.KEY_ENTRY_CY).buffer(P.KEY_ENTRY_D / 2.0, resolution=24)
    slot = sbox(cx - P.KEY_SLOT_W / 2.0, P.KEY_ENTRY_CY - P.KEY_DROP,
                cx + P.KEY_SLOT_W / 2.0, P.KEY_ENTRY_CY)
    return unary_union([entry, slot])


def build_shell():
    # ---- outer body ------------------------------------------------------------
    outer = rrect(0, 0, P.FACE_W, P.FACE_H, P.CASE_R)
    solid = prism(outer, 0.0, P.DEPTH)

    # ---- hollow it out: one cavity from the interior floor up to the face -------
    inner = rrect(P.WALL, P.WALL, P.FACE_W - P.WALL, P.FACE_H - P.WALL,
                  max(P.CASE_R - P.WALL, 1.0))
    cavity = prism(inner, P.FLOOR_Z, P.DEPTH)
    shell = difference([solid, cavity])

    adds, cuts = [], []

    # ---- PCB mounting bosses ---------------------------------------------------
    hx0 = P.PCB_X0 + P.HOLE_INSET
    hy0 = P.PCB_Y0 + P.HOLE_INSET
    holes = [(hx0, hy0), (hx0 + P.HOLE_DX, hy0),
             (hx0, hy0 + P.HOLE_DY), (hx0 + P.HOLE_DX, hy0 + P.HOLE_DY)]
    for (x, y) in holes:
        adds.append(cyl(P.BOSS_OD / 2.0, P.FLOOR_Z, P.PCB_BACK_Z, x, y))
        cuts.append(cyl(P.BOSS_PILOT / 2.0, P.FLOOR_Z - 1.0, P.PCB_BACK_Z, x, y, seg=32))

    # ---- face-plate screw bosses (four corners of the face) --------------------
    fs = P.FSCREW_INSET
    fscrews = [(fs, fs), (P.FACE_W - fs, fs),
               (fs, P.FACE_H - fs), (P.FACE_W - fs, P.FACE_H - fs)]
    for (x, y) in fscrews:
        adds.append(cyl(P.BOSS_OD / 2.0, P.FLOOR_Z, P.GLASS_Z, x, y))
        cuts.append(cyl(P.FSCREW_PILOT / 2.0, P.GLASS_Z - 12.0, P.GLASS_Z + 1.0, x, y, seg=32))

    # ---- keyhole wall mount ----------------------------------------------------
    # Through the rear wall, plus a shallow relief inside so the captured screw head
    # has somewhere to sit while the unit slides down.
    for cx in P.KEY_X:
        kp = keyhole_poly(cx)
        cuts.append(prism(kp, -1.0, P.REAR_WALL + 0.01))
        cuts.append(prism(kp.buffer(1.5, resolution=16),
                          P.REAR_WALL, P.REAR_WALL + P.KEY_HEAD_CLR))

    # ---- USB-C breakout cradle -------------------------------------------------
    # The board stands on edge: plane perpendicular to the wall, receptacle at z~0.
    t = P.UC_T + P.UC_SLOT_CLR
    x0, x1 = P.UC_CX - t / 2.0, P.UC_CX + t / 2.0
    y0, y1 = P.UC_CY - P.UC_H / 2.0, P.UC_CY + P.UC_H / 2.0
    rib = 2.5
    # Two ribs forming the slot. They start at the interior floor (the receptacle itself
    # occupies the rear-wall thickness, nested in the port cutout) and run forward to the
    # board's rear edge at UC_Z1.
    adds.append(bx(x0 - rib, y0 - 1.0, P.FLOOR_Z, x0, y1 + 1.0, P.UC_Z1))
    adds.append(bx(x1, y0 - 1.0, P.FLOOR_Z, x1 + rib, y1 + 1.0, P.UC_Z1))
    # end stop so the board cannot be pushed forward off the port
    adds.append(bx(x0 - rib, y0 - 1.0, P.UC_Z1, x1 + rib, y1 + 1.0, P.UC_Z1 + 2.0))
    # screw bosses either side of the receptacle, at the board's hole depth
    for dy in (-P.UC_HOLE_CC / 2.0, +P.UC_HOLE_CC / 2.0):
        for xb in (x0 - rib, x1):
            adds.append(bx(xb, P.UC_CY + dy - 3.0, P.FLOOR_Z,
                           xb + rib, P.UC_CY + dy + 3.0, P.UC_Z0 + P.UC_HOLE_OFF + 3.0))

    # ---- rear port + funnel ----------------------------------------------------
    port = bx(P.UC_CX - P.PORT_W / 2.0, P.UC_CY - P.PORT_H / 2.0, -1.0,
              P.UC_CX + P.PORT_W / 2.0, P.UC_CY + P.PORT_H / 2.0, P.REAR_WALL + 0.01)
    cuts.append(port)
    # outward funnel: a bigger opening on the wall face, tapering in
    f = P.PORT_FUNNEL
    funnel = bx(P.UC_CX - P.PORT_W / 2.0 - f, P.UC_CY - P.PORT_H / 2.0 - f, -1.0,
                P.UC_CX + P.PORT_W / 2.0 + f, P.UC_CY + P.PORT_H / 2.0 + f, 1.0)
    cuts.append(funnel)

    # ---- power cable channel: breakout (right) -> J10 (far LEFT) ---------------
    # A recess in the interior floor so the pair does not get pinched under the PCB.
    ch_y = P.PCB_Y0 + P.J10_Y
    cuts.append(bx(P.PCB_X0 + P.J10_X - 4.0, ch_y - P.CABLE_CH_W / 2.0, P.FLOOR_Z - 1.2,
                   P.UC_CX, ch_y + P.CABLE_CH_W / 2.0, P.FLOOR_Z + 0.01))

    # ---- assemble --------------------------------------------------------------
    shell = union([shell] + adds)
    shell = difference([shell] + cuts)
    return shell


if __name__ == "__main__":
    m = build_shell()
    m.export("shell.stl")
    print(f"shell.stl  watertight={m.is_watertight}  volume={m.volume/1000:.1f} cm^3  "
          f"bbox={np.round(m.extents,2)}")
