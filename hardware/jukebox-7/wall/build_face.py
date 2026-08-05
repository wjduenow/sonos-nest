#!/usr/bin/env python3
"""
Build face.stl -- the front plate of the sonos-jukebox 7" flush wall case.

  * rounded plate matching the shell footprint, FACE_T thick
  * screen opening over the 155 x 87 lit area, with a rebate that lands on the
    display's black border rather than on the lit area itself
  * control column: Ø36 dial hole + a 2x2 grid of Ø13 button holes
  * 4 countersunk face screws into the shell's bosses

  *** The screen opening's POSITION is provisional. *** AA_X0 / AA_Y0 in
  case_params.py are a centred guess until the lit area is measured on the real
  board; everything else here is fixed.

    conda run -n img23d python build_face.py      # -> face.stl
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, box, extrude_polygon
from trimesh.boolean import union, difference
from shapely.geometry import box as sbox
import case_params as P


def rrect(x0, y0, x1, y1, r):
    return sbox(x0 + r, y0 + r, x1 - r, y1 - r).buffer(r, resolution=24)


def prism(poly, z0, z1):
    m = extrude_polygon(poly, height=z1 - z0)
    m.apply_translation([0, 0, z0])
    return m


def cyl(r, z0, z1, x, y, seg=P.SEG):
    m = cylinder(radius=r, height=z1 - z0, sections=seg)
    m.apply_translation([x, y, (z0 + z1) / 2.0])
    return m


def screen_rect():
    """Lit-area rectangle in face coordinates."""
    x0 = P.PCB_X0 + P.AA_X0
    y0 = P.PCB_Y0 + P.AA_Y0
    return x0, y0, x0 + P.AA_W, y0 + P.AA_H


def build_face():
    z0, z1 = P.GLASS_Z, P.DEPTH          # 19.5 .. 22.0
    plate = prism(rrect(0, 0, P.FACE_W, P.FACE_H, P.CASE_R), z0, z1)

    cuts = []

    # ---- screen opening --------------------------------------------------------
    # Straight through at the lit area, with a 1 mm rebate on the inside so the
    # plate overlaps the black border and hides the glass edge.
    sx0, sy0, sx1, sy1 = screen_rect()
    cuts.append(prism(rrect(sx0, sy0, sx1, sy1, 2.0), z0 - 1.0, z1 + 1.0))
    cuts.append(prism(rrect(sx0 - 1.0, sy0 - 1.0, sx1 + 1.0, sy1 + 1.0, 2.0),
                      z0 - 1.0, z0 + 1.0))

    # ---- control column: dial + 4 buttons --------------------------------------
    cuts.append(cyl((P.DIAL_D + P.DIAL_CLR) / 2.0, z0 - 1.0, z1 + 1.0,
                    P.COL_CX, P.DIAL_CY))
    for by in P.BTN_ROW_Y:
        for dx in (-P.BTN_PITCH / 2.0, +P.BTN_PITCH / 2.0):
            cuts.append(cyl((P.BTN_D + P.BTN_CLR) / 2.0, z0 - 1.0, z1 + 1.0,
                            P.COL_CX + dx, by))

    # ---- face screws (countersunk from the front) ------------------------------
    for (x, y) in P.FSCREWS:
        cuts.append(cyl(P.FSCREW_PILOT / 2.0 + 0.3, z0 - 1.0, z1 + 1.0, x, y, seg=32))
        # conical countersink under the front surface
        cs = trimesh.creation.cone(radius=P.FSCREW_HEAD / 2.0, height=P.FSCREW_HEAD / 2.0,
                                   sections=32)
        cs.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
        cs.apply_translation([x, y, z1 - P.FSCREW_HEAD / 2.0 + 0.01])
        cuts.append(cs)

    return difference([plate] + cuts)


if __name__ == "__main__":
    m = build_face()
    m.export("face.stl")
    note = "MEASURED" if P.AA_MEASURED else "*** PROVISIONAL (centred guess) ***"
    print(f"face.stl   watertight={m.is_watertight}  volume={m.volume/1000:.1f} cm^3  "
          f"bbox={np.round(m.extents,2)}")
    print(f"           screen opening at {np.round(screen_rect(),2)}  -- {note}")
