"""button-v3 lid — the rear plate, and the part that actually carries the board.

Unusual for a lid, and the reason is in button_params.py §5: the board loads from the REAR, so
nothing shell-integral may stand behind it. The four pillars that the board screws onto therefore
have to belong to this part.

Geometry helpers and the outer profile come from build_shell, so the two parts cannot drift apart
on the seam that runs all the way round the box.

The lid no longer carries the board — that moved to build_carrier.py once the M2 x 18 screws it
implied proved unreasonable. What it does now is press the carrier forward: the spigot reaches all
the way to the carrier's back face, so the lid holds the whole stack against the front wall.
"""
import numpy as np
import trimesh
import button_params as P
from build_shell import full_outer, blk, cyl_y, ENG


def lid_body():
    return trimesh.boolean.intersection(
        [full_outer(), blk(-P.OUT_X, P.OUT_X, P.OUT_Y_SHELL, P.OUT_Y + 1.0, -1.0, P.HEIGHT + 1.0)],
        engine=ENG)


def spigot():
    """A shallow tongue into the cavity mouth.

    Two screws at the top would otherwise let the lid rack about them — there is nowhere at the
    bottom to put a third (see LID_POST_X in the params). This locates all four edges instead.

    A RING, not a plate. As a solid plate it collided with the shell's lid-screw posts, which
    stand in the same cavity mouth — caught by the shell-vs-lid interference test below, not by
    eye, and not by any per-part check. Both parts were watertight and correct in isolation.
    """
    c = P.SPIGOT_CLR
    outer = blk(-P.IN_X / 2 + c, P.IN_X / 2 - c,
                P.OUT_Y_SHELL - P.SPIGOT_T, P.OUT_Y_SHELL + 0.01,
                P.WALL + c, P.HEIGHT - P.WALL - c)
    inner = blk(-P.IN_X / 2 + c + P.SPIGOT_W, P.IN_X / 2 - c - P.SPIGOT_W,
                P.OUT_Y_SHELL - P.SPIGOT_T - 1.0, P.OUT_Y_SHELL + 1.0,
                P.WALL + c + P.SPIGOT_W, P.HEIGHT - P.WALL - c - P.SPIGOT_W)
    return trimesh.boolean.difference([outer, inner], engine=ENG)


def post_reliefs():
    """Clearance where the shell's lid-screw posts pass through the spigot ring."""
    return trimesh.boolean.union(
        [cyl_y(P.LID_POST_OD + 2 * P.SPIGOT_CLR,
               P.OUT_Y_SHELL - P.SPIGOT_T - 1.0, P.OUT_Y_SHELL + 0.01,
               x=sx * P.LID_POST_X, z=P.LID_POST_Z) for sx in (-1, 1)], engine=ENG)


def screw_head_reliefs():
    """Pockets where the carrier's four M2 heads sit under the spigot ring.

    The heads land at |x| = 15.8-16.2, and the ring runs 16.335 to 18.335 — they overlap. Relieving
    the ring is the right fix rather than counterboring the carrier: a counterbore would shorten
    the screw's clearance run and push engagement past PCB_T, i.e. through the board into the LCD.
    """
    return trimesh.boolean.union(
        [cyl_y(P.SCREW_HEAD_D + 2 * P.SPIGOT_CLR,
               P.OUT_Y_SHELL - P.SPIGOT_T - 0.01, P.OUT_Y_SHELL - P.SPIGOT_T + P.SCREW_HEAD_T,
               x=x, z=z) for x in P.HOLE_XS for z in P.HOLE_ZS], engine=ENG)


def lid_screw_holes():
    return trimesh.boolean.union(
        [cyl_y(P.LID_SCREW_D, P.OUT_Y_SHELL - P.SPIGOT_T - 1.0, P.OUT_Y + 1.0,
               x=sx * P.LID_POST_X, z=P.LID_POST_Z) for sx in (-1, 1)], engine=ENG)


def build_lid():
    m = trimesh.boolean.union([lid_body(), spigot()], engine=ENG)
    m = trimesh.boolean.difference([m, lid_screw_holes(), post_reliefs(), screw_head_reliefs()],
                                   engine=ENG)
    return m


if __name__ == "__main__":
    m = build_lid()

    # The lid's whole reason for existing is those four bores. If a boolean ever silently drops
    # one, the board has nothing to screw to and the part still looks fine — so count them.
    #
    # Counted on the OUTER face rather than by inspecting solids: four pillar bores plus two lid
    # screw holes = six holes through the plane at y = OUT_Y.
    sec = m.section(plane_origin=(0, P.OUT_Y - 0.5, 0), plane_normal=(0, 1, 0))
    loops = len(sec.discrete) if sec is not None else 0
    want = 1 + 2              # outline + the two lid screws
    assert loops == want, f"the lid face must show {want} loops (outline + 2 holes); it has {loops}"

    # ⚠️ THE CHECK THAT EARNED ITS PLACE. Each part can be watertight, pass every one of its own
    # clearance rows, and still be un-assemblable, because nothing in a per-part check looks at
    # the OTHER part. The first build was exactly that: shell and lid both perfect, 0.064 cm3 of
    # solid in the same place — the shell's lid posts standing inside the lid's spigot.
    from build_shell import build_shell
    from build_carrier import build_carrier
    sh, ca = build_shell(), build_carrier()
    for a, b, na, nb in ((sh, ca, "shell", "carrier"), (sh, m, "shell", "lid"),
                         (ca, m, "carrier", "lid")):
        v = trimesh.boolean.intersection([a, b], engine=ENG).volume / 1000.0
        assert v < 1e-3, f"{na} and {nb} occupy the same space: {v:.4f} cm3"
        print(f"  interference  {na:7} n {nb:7} = {v:.5f} cm3  ok")

    # The spigot has to actually REACH the carrier, or nothing holds the stack forward and the
    # board floats on 6.48 mm of nothing. SPIGOT_T is derived to make this exact; assert it stays
    # exact, because a gap here is invisible in every other check.
    gap = (P.OUT_Y_SHELL - P.SPIGOT_T) - P.CARRIER_Y1
    assert abs(gap) < 1e-6, f"spigot does not meet the carrier: {gap:+.3f} mm"

    m.export("lid.stl")
    print(f"  spigot     {P.SPIGOT_T:.2f} deep — bears on the carrier's back face")
    print(f"  screws     2x M3 x 8 (lid into the shell posts)")
    print(f"  lid.stl    watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
    print(f"             bbox={np.round(m.bounds, 2).tolist()}")
