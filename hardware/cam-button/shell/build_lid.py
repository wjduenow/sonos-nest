#!/usr/bin/env python3
"""
Build lid.stl -- the TAPE FACE of the `sonos-button` case.

This part is the whole reason the box is laid out the way it is.  The unit hangs from the
underside of a nightstand on double-sided tape with the button pointing DOWN, so the face
opposite the button has exactly one job: be a big, flat, uninterrupted slab of plastic.

    OUT_X x OUT_Y = 48.2 x 44.2 -> ~2130 mm^2 of adhesive area, minus 4 countersinks.

Pressing the button pushes the case UP into the nightstand, so this joint lives in
COMPRESSION and the tape only ever carries the box's own ~40 g.  That is why there is no
flange, no lip and no pull-out feature here -- just a plate and four screws.

Why the screws are FLAT-head and not the round heads the sleep-machine's bezel uses: a
round head stands proud, and anything proud of this face is a bump under the adhesive.
../../rec-2.8/countertop/README.md already offers the flat head as an option ("a 90° flat
head seats flush if you prefer"); here it is mandatory.

FOUR holes, and only four -- asserted at the bottom of this file.  Nothing else earns a
hole in the adhesive face; see button_params.py's BOOT/RESET note for the one that was
tried and cut.

    python3 build_lid.py   # -> lid.stl

Printed FLAT, TAPE FACE DOWN: the adhesive then lands on a bed-smooth surface, which is
the best face an FDM printer can make, and the countersinks open upward so they need no
support.  Geometry lives in button_params.py.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, cone, extrude_polygon
from trimesh.boolean import union, difference
from trimesh.transformations import rotation_matrix
from shapely.geometry import box as sbox
import button_params as P

SEG = P.SEG


def rrect(hx, hy, r):
    return sbox(-(hx - r), -(hy - r), (hx - r), (hy - r)).buffer(r, resolution=16)


def countersink(head_d, shank_d, thk):
    """Through shank + a cone flaring out to the TOP face (z = thk).

    Same idiom as ../../rec-2.8/countertop/build_bezel.py -- deliberately, so the two
    parts take the same screw."""
    shank = cylinder(radius=shank_d / 2, height=thk + 0.4, sections=SEG)
    shank.apply_translation([0, 0, (thk + 0.4) / 2 - 0.2])
    csk_h = (head_d - shank_d) / 2 + 0.6
    c = cone(radius=head_d / 2, height=csk_h, sections=SEG)      # apex is at +Z
    c.apply_transform(rotation_matrix(np.pi, [1, 0, 0]))          # flip: wide end up
    c.apply_translation([0, 0, thk + 0.01])                       # flush with the top
    return union([shank, c], engine='manifold')


def build_lid(world=False):
    """Local frame: z = 0 is the lid's underside (the inside), z = LID_T is the TAPE FACE.
    Pass world=True to place it at its assembled height (z = CEIL_Z .. HEIGHT)."""
    plate = extrude_polygon(rrect(P.OUT_X / 2, P.OUT_Y / 2, P.OUT_R), P.LID_T)

    cuts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = countersink(P.LID_SCREW_HEAD, P.LID_SCREW_D, P.LID_T)
            h.apply_translation([sx * P.LID_POST_X, sy * P.LID_POST_Y, 0])
            cuts.append(h)

    # No BOOT / RESET pin holes: this face is the adhesive face, and they would be the
    # only holes in it that buy nothing.  Reasoning lives in button_params.py.
    lid = difference([plate] + cuts, engine='manifold')
    if world:
        lid.apply_translation([0, 0, P.CEIL_Z])
    solids = [p for p in lid.split(only_watertight=False) if p.volume > 1.0]
    lid = solids[0] if len(solids) == 1 else trimesh.util.concatenate(solids)
    return lid


if __name__ == "__main__":
    m = build_lid()
    m.export("lid.stl")
    tape = P.OUT_X * P.OUT_Y
    print(f"lid.stl    watertight={m.is_watertight}  winding={m.is_winding_consistent}  "
          f"volume={m.volume/1000:.2f} cm^3  tris={len(m.faces)}")
    print(f"           bbox(XxYxZ)={np.round(m.extents, 2)} mm")
    # A plate with n through-holes has euler = 2 - 2n, so 4 holes -> -6.  This is here to
    # stop a hole quietly creeping back into the adhesive face.
    holes = (2 - m.euler_number) // 2
    assert holes == 4, f"the tape face must have exactly 4 holes, this lid has {holes}"
    print(f"           tape face {P.OUT_X:.1f} x {P.OUT_Y:.1f} = {tape:.0f} mm^2, "
          f"{holes} holes (Ø{P.LID_SCREW_HEAD} countersinks) and nothing else")
