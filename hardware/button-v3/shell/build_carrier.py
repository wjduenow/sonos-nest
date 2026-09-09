"""button-v3 carrier — the plate the board actually screws to.

Exists so the board joint happens where there is ACCESS. The board's back face sits 15.98 mm in
from the outside of the box, so a screw driven from the rear face has to span that: M2 x 18, thin,
aimed blind at a brass thread you cannot see. Neither a thicker lid nor shorter pillars fix it —
the distance is set by the window at one end and the box depth at the other.

So the board is screwed to this in the open, with everything visible, and the pair drops into the
shell as one piece. The lid's spigot then bears on the carrier's back face and holds the stack
forward against the front wall.
"""
import numpy as np
import trimesh
import button_params as P
from build_shell import blk, cyl_y, ENG


def plate():
    """Spans the cavity over the BOARD region only.

    ⚠️ It must stop short of the button: the M12 nut sweeps 19.48 across at z = 2.0..4.0 and
    y = 2.5..21.98, which is exactly where this plate's depth sits. Running it to the top of the
    cavity to give the spigot a full-perimeter seat would drive it straight through the nut.
    """
    c = P.CARRIER_CLR
    return blk(-P.IN_X / 2 + c, P.IN_X / 2 - c,
               P.CARRIER_Y0, P.CARRIER_Y1,
               P.PCB_TOP_Z, P.HEIGHT - P.WALL - c)


def posts():
    """Stand off over the components to reach the PCB's back face at the four eyelets."""
    return trimesh.boolean.union(
        [cyl_y(P.CARRIER_POST_OD, P.BOARD_Y_PCB_BACK, P.CARRIER_Y0 + 0.01, x=x, z=z)
         for x in P.HOLE_XS for z in P.HOLE_ZS], engine=ENG)


def lid_post_reliefs():
    """Clearance where the shell's LOWER lid posts pass through the plate.

    A consequence of centring the screen: growing the box downward is what made room for the lower
    posts, and the same growth let this plate reach down over them. The upper pair is clear because
    the plate starts at the board's top edge.
    """
    return trimesh.boolean.union(
        [cyl_y(P.LID_POST_OD + 2 * P.CARRIER_CLR,
               P.CARRIER_Y0 - 1.0, P.CARRIER_Y1 + 1.0, x=sx * P.LID_POST_X, z=z)
         for sx in (-1, 1) for z in P.LID_POST_ZS
         if P.PCB_TOP_Z <= z <= P.HEIGHT], engine=ENG)


def bores():
    return trimesh.boolean.union(
        [cyl_y(P.CARRIER_BORE, P.BOARD_Y_PCB_BACK - 1.0, P.CARRIER_Y1 + 1.0, x=x, z=z)
         for x in P.HOLE_XS for z in P.HOLE_ZS], engine=ENG)


def build_carrier():
    m = trimesh.boolean.union([plate(), posts()], engine=ENG)
    return trimesh.boolean.difference([m, bores(), lid_post_reliefs()], engine=ENG)


if __name__ == "__main__":
    m = build_carrier()

    # ⚠️ The screw is pinned from both ends and must be checked, not chosen. Too short and it never
    # reaches the thread; too long and it drives through the PCB into the back of the LCD, which no
    # amount of care recovers. A brass eyelet in a 1.6 mm board offers at most PCB_T of thread.
    assert 1.0 <= P.BOARD_SCREW_ENGAGE <= P.PCB_T, (
        f"M2 x {P.BOARD_SCREW_LEN:.0f} engages {P.BOARD_SCREW_ENGAGE:.2f} mm; "
        f"needs 1.0 .. {P.PCB_T} — retune CARRIER_T")

    # Four posts, four bores. A dropped boolean would leave a plate that looks fine and holds
    # nothing: count the loops through the post band.
    sec = m.section(plane_origin=(0, P.BOARD_Y_PCB_BACK + P.CARRIER_POST_LEN / 2, 0),
                    plane_normal=(0, 1, 0))
    loops = len(sec.discrete) if sec is not None else 0
    assert loops == 8, f"the post band must show 8 loops (4 posts, wall + bore each); it has {loops}"

    m.export("carrier.stl")
    print(f"  screws     4x M2 x {P.BOARD_SCREW_LEN:.0f}  (crosses {P.BOARD_SCREW_PASS:.2f}, "
          f"engages {P.BOARD_SCREW_ENGAGE:.2f} of max {P.PCB_T})")
    print(f"  carrier.stl watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
    print(f"              bbox={np.round(m.bounds, 2).tolist()}")
