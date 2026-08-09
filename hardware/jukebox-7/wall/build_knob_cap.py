#!/usr/bin/env python3
"""
Build knob_cap.stl -- the printed dial cap for the sonos-jukebox control column.

Fits the Arduino Modulino Knob's encoder directly: Bourns PEC11J-9215F-S0015,
Ø6.0 shaft with a D-flat over the last 5 mm at 4.5 mm across. No set screw, no
insert, no hardware -- the D-bore keys it.

  * Ø36 x 14 mm, per the design system's --knob-dia / --knob-height
  * knurled rim (36 scallops) and a 1 mm chamfer on the top edge
  * STEPPED BORE: round for the first 3.5 mm, D above it

    The step is not decoration. Only the top 5 mm of the shaft is flatted; below
    that it is round Ø6. A D-bore running the full depth would jam on that round
    section and never seat -- the flat is 4.5 across, narrower than the Ø6 shaft
    it would have to pass over.

PRINT IT TOP-FACE-DOWN. The bore then opens upward as a plain vertical hole with
no overhang and no supports, and the visible top face is laid against the bed.

    conda run -n img23d python build_knob_cap.py   # -> knob_cap.stl
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, extrude_polygon, revolve
from trimesh.boolean import difference
from shapely.geometry import Point, box as sbox
from shapely.ops import unary_union
import case_params as P


def d_profile(dia, across, resolution=64):
    """Circle with one side flattened -- the shaft's D section, as a polygon."""
    r = dia / 2.0
    c = Point(0, 0).buffer(r, resolution=resolution)
    return c.intersection(sbox(-2 * r, -2 * r, 2 * r, across - r))


def build_cap():
    R = P.KCAP_D / 2.0
    H = P.KCAP_H
    ch = P.KCAP_CHAMFER

    # ---- knurl in 2D, then ONE extrusion ----------------------------------------
    # Subtracting 42 cylinders from a revolved solid is a fragile boolean -- the flute
    # centres lie exactly ON the outer surface, so every cut grazes it tangentially and
    # the result came out non-watertight at Ø42. Doing the scallops in 2D with shapely
    # and extruding once is exact, and drops ~40 3D booleans.
    disc = Point(0, 0).buffer(R, resolution=128)
    flutes = unary_union([
        Point(R * np.cos(2 * np.pi * i / P.KCAP_FLUTES),
              R * np.sin(2 * np.pi * i / P.KCAP_FLUTES)).buffer(P.KCAP_FLUTE_D / 2.0,
                                                               resolution=12)
        for i in range(P.KCAP_FLUTES)])
    body = extrude_polygon(disc.difference(flutes), height=H)

    cuts = []

    # ---- top-edge chamfer: built from two primitives, not a revolve --------------
    # The tool is (a big disc) minus (a 45 deg cone), which leaves exactly the ring of
    # material outside the chamfer face. Both are clean solids, so the boolean is safe.
    big = cylinder(radius=R + 6.0, height=ch + 2.0, sections=128)
    big.apply_translation([0, 0, H - ch + (ch + 2.0) / 2.0])
    cone = trimesh.creation.cone(radius=R, height=R, sections=128)
    cone.apply_translation([0, 0, H - ch])
    cuts.append(difference([big, cone]))

    # ---- bore: round low, D high ------------------------------------------------
    bore_d = P.KCAP_SHAFT_D + P.KCAP_FIT
    flat = P.KCAP_FLAT + P.KCAP_FIT

    # round section, from the underside up
    cuts.append(cylinder(radius=bore_d / 2.0, height=P.KCAP_ROUND_H * 2, sections=64))

    # D section above it
    dsec = extrude_polygon(d_profile(bore_d, flat), height=P.KCAP_BORE_H - P.KCAP_ROUND_H + 1)
    dsec.apply_translation([0, 0, P.KCAP_ROUND_H])
    cuts.append(dsec)

    # Lead-in at the bore mouth, as a COUNTERBORE rather than a cone. The cone version
    # tapered to a point at KCAP_LEADIN, so above ~0.1 mm it sat entirely inside the bore
    # -- redundant overlap that produced degenerate faces and a non-watertight STL. A
    # short wider cylinder starts the shaft just as well and is a clean primitive.
    lead = cylinder(radius=bore_d / 2.0 + P.KCAP_LEADIN,
                    height=P.KCAP_LEADIN * 2.0, sections=64)
    cuts.append(lead)

    cap = difference([body] + cuts)

    # The 2D knurl leaves a few sliver triangles where each scallop meets the disc.
    # They are harmless in memory but do not survive an STL round-trip -- the reloaded
    # mesh comes back non-watertight. Clean them out before returning.
    cap.update_faces(cap.nondegenerate_faces())
    cap.merge_vertices()
    cap.fix_normals()
    return cap


if __name__ == "__main__":
    m = build_cap()
    m.export("knob_cap.stl")
    engaged = (P.DEPTH + P.KCAP_H) - (P.KNOB_TIP_Z) if False else None
    shaft_in = P.KNOB_TIP_Z - (P.DEPTH + P.KCAP_GAP)
    print(f"knob_cap.stl  watertight={m.is_watertight}  volume={m.volume/1000:.2f} cm^3  "
          f"bbox={np.round(m.extents, 2)}")
    print(f"              Ø{P.KCAP_D} x {P.KCAP_H} mm, bore {P.KCAP_SHAFT_D}+{P.KCAP_FIT} "
          f"round {P.KCAP_ROUND_H} then D at {P.KCAP_FLAT}+{P.KCAP_FIT} across")
    print(f"              shaft engagement {shaft_in:.1f} mm of a {P.KCAP_BORE_H} mm bore")
    print(f"              PRINT TOP-FACE-DOWN (bore opening up, no supports)")
