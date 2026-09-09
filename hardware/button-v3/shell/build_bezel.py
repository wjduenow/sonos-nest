"""button-v3 bezel — the front part. Carries the window, locates the board, and clamps it.

This part is why the case is only two pieces. Making the front removable let the board load from
the FRONT, which meant the body could grow its own posts — and once the board rests on four
coplanar posts, a rigid board stays flat under a clamping force applied anywhere inside their
footprint. So this bezel holds it with an L-shaped lip along the chin and bottom edges, and no
screw ever enters the board.

The window and the locating rim are on THIS part, together. That is the point: there is no
tolerance stack between where the board sits and where the hole is, which is what the old
three-part design could never manage.
"""
import numpy as np
import trimesh
import button_params as P
from build_body import full_outer, blk, cyl_y, ENG, SEG


def cone_y(d, y0, y1, x=0.0, z=0.0):
    """A 90-degree countersink, wide end at y0."""
    m = trimesh.creation.cone(radius=d / 2, height=y1 - y0, sections=SEG)
    m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, (1, 0, 0)))
    m.apply_translation((x, y0, z))
    return m


def bezel_body():
    return trimesh.boolean.intersection(
        [full_outer(), blk(-P.OUT_X, P.OUT_X, -1.0, P.BEZEL_T, -1.0, P.HEIGHT + 1.0)], engine=ENG)


def window():
    return blk(P.WIN_X0, P.WIN_X1, -1.0, P.BEZEL_T + 1.0, P.WIN_Z0, P.WIN_Z1)


def locating_rim():
    """Three sides, not four — and the missing one is deliberate.

    The board is offset right so the lit area centres on the case, which leaves it 0.4 mm from the
    right cavity wall: a rim there could be at most 0.25 mm, well under a nozzle. So the body's own
    wall locates that edge and this rim does the other three, pushing the board against it. That is
    still a defined, repeatable position — and it costs no extra width, whereas a fourth side would
    have needed 2.4 mm more box.
    """
    xo = P.BOARD_X0 - P.POCKET_CLR - P.RIM_W          # -17.75
    xi = P.BOARD_X0 - P.POCKET_CLR                    # -16.55
    xr = min(P.BOARD_X1 + P.POCKET_CLR + P.RIM_W, P.IN_X_HALF - 0.25)
    zt_o = P.PCB_TOP_Z - P.POCKET_CLR - P.RIM_W
    zt_i = P.PCB_TOP_Z - P.POCKET_CLR
    zb_i = P.PCB_BOT_Z + P.POCKET_CLR
    zb_o = zb_i + P.RIM_W
    y0, y1 = P.BEZEL_T, P.BEZEL_T + P.POCKET_D
    return trimesh.boolean.union([
        blk(xo, xi, y0, y1, zt_o, zb_o),      # left
        blk(xo, xr, y0, y1, zt_o, zt_i),      # top
        blk(xo, xr, y0, y1, zb_i, zb_o),      # bottom
    ], engine=ENG)


def screw_holes():
    return trimesh.boolean.union(
        [cyl_y(P.BEZEL_SCREW_D, -1.0, P.BEZEL_T + 1.0, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def countersinks():
    d = P.BEZEL_SCREW_HEAD_D
    return trimesh.boolean.union(
        [cone_y(d, 0.0, d / 2.0, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def build_bezel():
    m = trimesh.boolean.union([bezel_body(), locating_rim()], engine=ENG)
    return trimesh.boolean.difference([m, window(), screw_holes(), countersinks()], engine=ENG)


if __name__ == "__main__":
    from build_body import build_body
    m = build_bezel()

    # Outline + window + four screws. A dropped boolean here is a bezel that looks right and either
    # covers the screen or cannot be fastened.
    sec = m.section(plane_origin=(0, P.BEZEL_T / 2, 0), plane_normal=(0, 1, 0))
    loops = len(sec.discrete) if sec is not None else 0
    assert loops == 6, f"the bezel face must show 6 loops (outline + window + 4 screws); it has {loops}"

    body = build_body()
    v = trimesh.boolean.intersection([body, m], engine=ENG).volume / 1000.0
    assert v < 1e-3, f"body and bezel occupy the same space: {v:.4f} cm3"
    print(f"  interference  body n bezel = {v:.5f} cm3  ok")

    m.export("bezel.stl")
    print(f"  screws     4x M3 x {P.BEZEL_SCREW_LEN:.0f}, countersunk, into the body's corner bosses")
    print(f"  rim        3 sides, {P.RIM_W} wall, {P.POCKET_D} deep, {P.POCKET_CLR} clearance")
    print(f"  bezel.stl  watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
