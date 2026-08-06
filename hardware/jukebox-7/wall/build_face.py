#!/usr/bin/env python3
"""
Build face.stl -- the front plate of the sonos-jukebox 7" flush wall case.

  * rounded plate matching the shell footprint, FACE_T thick
  * screen opening over the 155 x 87 lit area, with a rebate that lands on the
    display's black border rather than on the lit area itself
  * control column: Ø36 dial hole + a 2x2 grid of Ø13 button holes
  * 6 magnet spigots -- no screws, nothing breaks the front surface

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
    """The OPENING: the display MODULE's outline plus clearance, in face coordinates.

    Not the lit area. This is a wrap-around bezel -- the module passes up through the
    face plate and its front surface finishes flush with the plate's, so the opening
    has to clear the module, not frame the lit area.
    """
    c = P.SCREEN_CLR / 2.0
    x0 = P.PCB_X0 + P.GLASS_X0 - c
    y0 = P.PCB_Y0 + P.GLASS_Y0 - c
    return x0, y0, x0 + P.GLASS_W + P.SCREEN_CLR, y0 + P.GLASS_H + P.SCREEN_CLR


def build_face():
    z0, z1 = P.FACE_Z0, P.DEPTH          # 18.0 .. 20.5
    plate = prism(rrect(0, 0, P.FACE_W, P.FACE_H, P.CASE_R), z0, z1)

    cuts = []

    # ---- screen opening --------------------------------------------------------
    # Straight through, sized to the MODULE. No rebate: nothing overlaps the glass,
    # because the glass finishes flush with this plate's front face.
    sx0, sy0, sx1, sy1 = screen_rect()
    cuts.append(prism(rrect(sx0, sy0, sx1, sy1, 2.0), z0 - 1.0, z1 + 1.0))

    # ---- control column: dial + 4 buttons --------------------------------------
    # Only the encoder's Ø7 bushing passes through here; the Ø36 cap sits proud on top
    # and overhangs the opening by 13.5 mm all round.
    cuts.append(cyl(P.DIAL_HOLE_D / 2.0, z0 - 1.0, z1 + 1.0, P.COL_CX, P.DIAL_CY))
    for by in P.BTN_ROW_Y:
        for dx in (-P.BTN_PITCH / 2.0, +P.BTN_PITCH / 2.0):
            cuts.append(cyl((P.BTN_D + P.BTN_CLR) / 2.0, z0 - 1.0, z1 + 1.0,
                            P.COL_CX + dx, by))

    # ---- magnet spigots ---------------------------------------------------------
    # NOTHING breaks the front surface. Each magnet lives in a boss that drops below the
    # mating plane, so there is 4.3 mm of material between the disc and the outside, the
    # two discs touch with no plastic between them, and the six bosses register the plate
    # against sliding -- which matters, because magnets are weak in shear.
    # The spigot stops MAG_AIRGAP short of the shell magnet, so the plate seats on the
    # rim and the blocks rather than bottoming out on a disc.
    spigots = [cyl(P.MAG_SPIGOT_D / 2.0, P.MAG_SPIGOT_Z0, z0 + 0.01, x, y)
               for (x, y) in P.MAGNETS]
    for (x, y) in P.MAGNETS:
        cuts.append(cyl(P.MAG_POCKET_D / 2.0, P.MAG_SPIGOT_Z0 - 0.01,
                        P.MAG_SPIGOT_Z0 + P.MAG_POCKET_H, x, y))

    return difference([union([plate] + spigots)] + cuts)


if __name__ == "__main__":
    m = build_face()
    m.export("face.stl")
    note = "MEASURED" if P.AA_MEASURED else "*** PROVISIONAL (centred guess) ***"
    print(f"face.stl   watertight={m.is_watertight}  volume={m.volume/1000:.1f} cm^3  "
          f"bbox={np.round(m.extents,2)}")
    print(f"           screen opening at {np.round(screen_rect(),2)}  -- {note}")
