#!/usr/bin/env python3
"""
Build bezel.stl -- the screwed-on front frame that covers the board edges + corner
screws and frames the screen.  It rests on the glass front (glass is 4.30 mm proud
of the PCB) and on 4 posts in the shell's face margins; 4 countersunk screws pull
it down onto those posts, trapping the board.

  * outer: rounded rectangle matching the shell face top
  * opening: viewing area + a small margin (exposes the screen, hides the glass rim)
  * 4 countersunk screw holes over the shell's bezel posts

    python3 build_bezel.py   # -> bezel.stl  (exported lying flat for printing)

Geometry / board numbers live in stand_params.py.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, extrude_polygon, cone
from trimesh.boolean import difference, union
from trimesh.transformations import rotation_matrix, translation_matrix
from shapely.geometry import box as sbox
import stand_params as P

SEG = P.SEG
# same local-board-frame -> world transform as the shell (for the preview only)
_R = rotation_matrix(np.radians(90 - P.TILT_DEG), [1, 0, 0])
_sT, _cT = np.sin(np.radians(P.TILT_DEG)), np.cos(np.radians(P.TILT_DEG))
_s_center = P.MB + P.PCB_H / 2.0
_center = [0.0, P.FRONT_LIP + _s_center * _sT, _s_center * _cT]
_M = translation_matrix(_center) @ _R

def rrect(hx, hy, r):
    return sbox(-(hx - r), -(hy - r), (hx - r), (hy - r)).buffer(r, resolution=16)

def countersink(head_d, shank_d, thk):
    """Through shank + a cone that flares out to the TOP face (z = thk)."""
    shank = cylinder(radius=shank_d / 2, height=thk + 0.4, sections=SEG)
    shank.apply_translation([0, 0, (thk + 0.4) / 2 - 0.2])
    csk_h = (head_d - shank_d) / 2 + 0.6
    # cone() has its apex at +Z; flip so the wide end is at the top face
    c = cone(radius=head_d / 2, height=csk_h, sections=SEG)
    c.apply_transform(rotation_matrix(np.pi, [1, 0, 0]))           # wide end up
    c.apply_translation([0, 0, thk + 0.01])                        # flush at top
    return union([shank, c], engine='manifold')

def build_bezel(world=False):
    outer = extrude_polygon(rrect(P.BEZEL_OUT_X, P.BEZEL_OUT_Y, P.BEZEL_R), P.BEZEL_T)
    open_hx = P.VA_W / 2 + P.OPEN_MARGIN
    open_hy = P.VA_H / 2 + P.OPEN_MARGIN
    opening = extrude_polygon(rrect(open_hx, open_hy, 2.0), P.BEZEL_T + 1.0)
    opening.apply_translation([0, 0, -0.5])
    frame = difference([outer, opening], engine='manifold')
    cuts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = countersink(P.BEZEL_SCREW_HEAD, 3.2, P.BEZEL_T)
            h.apply_translation([sx * P.POST_X, sy * P.POST_Y, 0])
            cuts.append(h)
    # microphone port: through-hole + a funnel countersink on the visible front face
    mic = countersink(P.MIC_CSK_D, P.MIC_HOLE_D, P.BEZEL_T)
    mic.apply_translation([P.MIC_X, P.MIC_Y, 0])
    cuts.append(mic)
    frame = difference([frame] + cuts, engine='manifold')
    if world:
        # lift onto the glass plane (z_local = GLASS_PROUD) and place on the face
        frame.apply_translation([0, 0, P.GLASS_PROUD])
        frame.apply_transform(_M)
    return frame

if __name__ == "__main__":
    m = build_bezel(world=False); m.export("bezel.stl")
    print(f"bezel.stl  watertight={m.is_watertight}  tris={len(m.faces)}  "
          f"bbox={np.round(m.extents,1)}  mm")
