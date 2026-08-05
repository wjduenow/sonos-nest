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

    # ---- body: revolved profile, so the top edge chamfer is part of the solid ----
    profile = np.array([[0.0, 0.0], [R, 0.0], [R, H - ch], [R - ch, H], [0.0, H]])
    body = revolve(profile, sections=192)

    cuts = []

    # ---- knurl: scallops cut around the rim -------------------------------------
    for i in range(P.KCAP_FLUTES):
        t = 2 * np.pi * i / P.KCAP_FLUTES
        f = cylinder(radius=P.KCAP_FLUTE_D / 2.0, height=2 * H, sections=20)
        f.apply_translation([R * np.cos(t), R * np.sin(t), H / 2.0])
        cuts.append(f)

    # ---- bore: round low, D high ------------------------------------------------
    bore_d = P.KCAP_SHAFT_D + P.KCAP_FIT
    flat = P.KCAP_FLAT + P.KCAP_FIT

    # round section, from the underside up
    cuts.append(cylinder(radius=bore_d / 2.0, height=P.KCAP_ROUND_H * 2, sections=64))

    # D section above it
    dsec = extrude_polygon(d_profile(bore_d, flat), height=P.KCAP_BORE_H - P.KCAP_ROUND_H + 1)
    dsec.apply_translation([0, 0, P.KCAP_ROUND_H])
    cuts.append(dsec)

    # lead-in chamfer at the bore mouth so the cap starts onto the shaft easily
    lead = trimesh.creation.cone(radius=bore_d / 2.0 + P.KCAP_LEADIN,
                                 height=P.KCAP_LEADIN + 0.01, sections=64)
    lead.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
    lead.apply_translation([0, 0, P.KCAP_LEADIN])
    cuts.append(lead)

    return difference([body] + cuts)


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
