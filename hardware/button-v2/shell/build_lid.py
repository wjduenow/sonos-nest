#!/usr/bin/env python3
"""
Build lid.stl -- the TAPE FACE of the `button-v2` case, and the board's only hold-down.

Two jobs, and the second one is new.  On ../../cam-button the lid is purely a flat slab: the
board is screwed to four bosses through its own mounting holes.  THE XIAO HAS NO MOUNTING
HOLES, so here the lid is a structural part -- a rib on its underside bears on the board's RF
shield can and presses it onto the shell's ledges.  Take the lid off and the board is loose.

    OUT_X x OUT_Y = 36.78 x 26.76 -> ~984 mm^2 of adhesive area, minus 4 countersinks and
    (unlike the cam-button) 2 keyhole slots.

Why the rib bears on the SHIELD CAN and nothing else: it is flat, rigid, 12.6 x 10.6 mm, and
electrically dead.  The two alternatives are both connectors -- the USB-C and the u.FL -- and
pressing a board down through a connector puts the retention load into its solder joints,
which is the exact failure this whole retention scheme exists to avoid.  check_clearances()
in build_shell.py proves the rib misses both.

Pressing the button pushes the case UP into the nightstand, so the tape joint lives in
COMPRESSION and the adhesive only ever carries the box's own ~35 g.

Why the screws are FLAT-head: a round head stands proud, and anything proud of this face is a
bump under the adhesive.  ../../rec-2.8/countertop/README.md offers flat as an option; here it
is mandatory, exactly as on the cam-button.

SIX holes -- asserted at the bottom of this file.  The cam-button's lid allows only four and
argues at length against any more (see its button_params.py BOOT/RESET note).  The two extra
here are the keyhole slots, and they earn it by REPLACING the tape rather than sitting
alongside it: they cost ~9% of the adhesive area and buy a whole second mounting route.

    python3 build_lid.py   # -> lid.stl

Printed FLAT, TAPE FACE DOWN: the adhesive then lands on a bed-smooth surface, which is the
best face an FDM printer can make.  The countersinks, the keyhole head pockets and the rib all
open upward, so nothing needs support.  Geometry lives in button_params.py.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, cone, extrude_polygon
from trimesh.boolean import union, difference
from trimesh.transformations import rotation_matrix, translation_matrix
from shapely.geometry import box as sbox
import button_params as P

SEG = P.SEG


def rrect(hx, hy, r):
    return sbox(-(hx - r), -(hy - r), (hx - r), (hy - r)).buffer(r, resolution=16)


def countersink(head_d, shank_d, thk):
    """Through shank + a cone flaring out to the TOP face (z = thk).

    Same idiom as ../../cam-button/shell/build_lid.py and ../../rec-2.8/countertop/build_bezel.py
    -- deliberately, so all three parts take the same screw."""
    shank = cylinder(radius=shank_d / 2, height=thk + 0.4, sections=SEG)
    shank.apply_translation([0, 0, (thk + 0.4) / 2 - 0.2])
    csk_h = (head_d - shank_d) / 2 + 0.6
    c = cone(radius=head_d / 2, height=csk_h, sections=SEG)      # apex is at +Z
    c.apply_transform(rotation_matrix(np.pi, [1, 0, 0]))          # flip: wide end up
    c.apply_translation([0, 0, thk + 0.01])                       # flush with the top
    return union([shank, c], engine='manifold')


def keyhole(head_d, slot_w, travel, thk):
    """A keyhole cut through the full lid thickness: a round entry the screw HEAD passes
    through, a slot the SHANK slides into, running in +X.

    Cut clean through rather than pocketed.  A pocketed head recess would be stronger, but it
    would also have to be on the tape side -- and a recess in the adhesive face is a void under
    the tape, which is worse than a hole through it."""
    r_head = head_d / 2
    head = cylinder(radius=r_head, height=thk + 0.4, sections=SEG)
    head.apply_translation([-travel / 2, 0, (thk + 0.4) / 2 - 0.2])
    slot = trimesh.creation.box(
        extents=(travel, slot_w, thk + 0.4),
        transform=translation_matrix([0, 0, (thk + 0.4) / 2 - 0.2]))
    end = cylinder(radius=slot_w / 2, height=thk + 0.4, sections=SEG)
    end.apply_translation([travel / 2, 0, (thk + 0.4) / 2 - 0.2])
    return union([head, slot, end], engine='manifold')


def hold_down_rib():
    """The rib that makes this lid structural.  Bears on the RF shield can only.

    Its length is chosen so a build in which the board is a fraction out of position still
    lands on metal: LID_RIB_W is 10.0 against the can's 12.60, so there is 1.3 mm of slop per
    side before the rib overhangs an edge."""
    return trimesh.creation.box(
        extents=(P.LID_RIB_W, P.LID_RIB_T, P.LID_RIB_H),
        transform=translation_matrix([0, P.LID_RIB_Y, -P.LID_RIB_H / 2]))


def build_lid(world=False):
    """Local frame: z = 0 is the lid's underside (the inside), z = LID_T is the TAPE FACE.
    The hold-down rib hangs BELOW z = 0, into the cavity.
    Pass world=True to place it at its assembled height (z = CEIL_Z .. HEIGHT)."""
    plate = extrude_polygon(rrect(P.OUT_X / 2, P.OUT_Y / 2, P.OUT_R), P.LID_T)
    body = union([plate, hold_down_rib()], engine='manifold')

    cuts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = countersink(P.LID_SCREW_HEAD, P.LID_SCREW_D, P.LID_T)
            h.apply_translation([sx * P.LID_POST_X, sy * P.LID_POST_Y, 0])
            cuts.append(h)

    if P.KEYHOLE_ON:
        for sy in (-1, 1):
            k = keyhole(P.KEYHOLE_HEAD_D, P.KEYHOLE_SLOT_W, P.KEYHOLE_TRAVEL, P.LID_T)
            k.apply_translation([P.KEYHOLE_X, sy * P.KEYHOLE_Y, 0])
            cuts.append(k)

    # No BOOT / RESET pin holes -- see button_params.py.  The keyhole slots are the only
    # holes in this face that are not screw holes, and they replace the tape rather than
    # joining it.
    lid = difference([body] + cuts, engine='manifold')
    if world:
        lid.apply_translation([0, 0, P.CEIL_Z])
    solids = [p for p in lid.split(only_watertight=False) if p.volume > 1.0]
    lid = solids[0] if len(solids) == 1 else trimesh.util.concatenate(solids)
    return lid


if __name__ == "__main__":
    m = build_lid()
    m.export("lid.stl")
    tape = P.OUT_X * P.OUT_Y
    kh = 2 * (np.pi * (P.KEYHOLE_HEAD_D / 2) ** 2 + P.KEYHOLE_TRAVEL * P.KEYHOLE_SLOT_W) \
        if P.KEYHOLE_ON else 0.0
    print(f"lid.stl    watertight={m.is_watertight}  winding={m.is_winding_consistent}  "
          f"volume={m.volume/1000:.2f} cm^3  tris={len(m.faces)}")
    print(f"           bbox(XxYxZ)={np.round(m.extents, 2)} mm")
    # A plate with n through-holes has euler = 2 - 2n.  4 countersinks + 2 keyholes -> 6.
    # This is here to stop a SEVENTH hole quietly creeping into the adhesive face.
    holes = (2 - m.euler_number) // 2
    want = 6 if P.KEYHOLE_ON else 4
    assert holes == want, f"the tape face must have exactly {want} holes, this lid has {holes}"
    print(f"           tape face {P.OUT_X:.1f} x {P.OUT_Y:.1f} = {tape:.0f} mm^2, "
          f"{holes} holes")
    print(f"           keyhole slots cost {kh:.0f} mm^2 = {100*kh/tape:.1f}% of the adhesive area")
    print(f"           hold-down rib {P.LID_RIB_W} x {P.LID_RIB_T} projecting {P.LID_RIB_H:.2f} "
          f"onto the shield can")
