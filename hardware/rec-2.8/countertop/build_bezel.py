#!/usr/bin/env python3
"""
Build bezel.stl -- the screwed-on front frame that covers the board edges + corner
screws and frames the screen.

The GLASS SITS FLUSH WITH THE BEZEL TOP.  The frame is exactly GLASS_PROUD thick and
lands directly on the shell face (== the PCB front plane), so its top face ends level
with the glass.  Its opening is the glass outline, so the screen passes THROUGH the
frame; the frame lands on the bare PCB strips at |x| > GLASS_W/2, clamping the board.

  * outer: rounded rectangle matching the shell face top
  * opening: glass outline + GLASS_CLR per side (screen passes through, ends flush)
  * 4 countersunk screw holes into the shell's face-margin pilots
  * 4 blind reliefs in the underside for the proud board-screw heads
  * mic port: through-hole + funnel countersink on the visible face

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
    """Frame is BEZEL_T (== GLASS_PROUD) thick, sitting on the face at local z=0, so its
    top face ends level with the glass.  The opening is the GLASS outline (+GLASS_CLR per
    side) -- the screen passes through the frame rather than being capped by it."""
    outer = extrude_polygon(rrect(P.BEZEL_OUT_X, P.BEZEL_OUT_Y, P.BEZEL_R), P.BEZEL_T)
    open_hx = P.GLASS_W / 2 + P.GLASS_CLR
    open_hy = P.GLASS_H / 2 + P.GLASS_CLR
    opening = extrude_polygon(rrect(open_hx, open_hy, 1.0), P.BEZEL_T + 1.0)
    opening.apply_translation([0, 0, -0.5])
    frame = difference([outer, opening], engine='manifold')
    cuts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = countersink(P.BEZEL_SCREW_HEAD, 3.2, P.BEZEL_T)
            h.apply_translation([sx * P.POST_X, sy * P.POST_Y, 0])
            cuts.append(h)
    # blind reliefs in the UNDERSIDE for the 4 board-screw heads, which stand proud of
    # the PCB front face and now sit under the frame (open at z=0, up to HEAD_RELIEF_DEPTH).
    #
    # NOTE: the lower-right relief (+39,-21) sits only 4.12 mm from the mic port (40,-17),
    # leaving a 0.12 mm nominal wall between them.  That is below one extrusion width, so
    # the two WILL merge when sliced.  It is harmless -- the port still lands over the mic
    # (which is 4.12 mm out, well clear of the Ø6 relief) and the relief is open to the PCB
    # anyway.  It cannot be designed out: with the mic 4 mm above the screw, a Ø5.4 head
    # relief and a Ø2 port simply overlap.  If MIC_X turns out to be >= 41.65 (it is still
    # a photo estimate) the wall grows past 0.8 mm and they separate cleanly.
    for sx in (-1, 1):
        for sy in (-1, 1):
            r = cylinder(radius=P.HEAD_RELIEF_D / 2,
                         height=P.HEAD_RELIEF_DEPTH + 1.0, sections=SEG)
            r.apply_translation([sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2,
                                 (P.HEAD_RELIEF_DEPTH + 1.0) / 2 - 1.0])
            cuts.append(r)
    # microphone port: through-hole + a funnel countersink on the visible front face
    mic = countersink(P.MIC_CSK_D, P.MIC_HOLE_D, P.BEZEL_T)
    mic.apply_translation([P.MIC_X, P.MIC_Y, 0])
    cuts.append(mic)
    frame = difference([frame] + cuts, engine='manifold')
    if world:
        # underside lands on the face plane (local z = 0) -> top is flush with the glass
        frame.apply_transform(_M)
    return frame

if __name__ == "__main__":
    m = build_bezel(world=False); m.export("bezel.stl")
    print(f"bezel.stl  watertight={m.is_watertight}  tris={len(m.faces)}  "
          f"bbox={np.round(m.extents,1)}  mm")
