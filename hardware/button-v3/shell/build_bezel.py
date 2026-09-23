"""button-v3 bezel — the front frame. Wraps the LCD and finishes the face flush.

The opening is the FULL board outline plus a hairline: the whole glass shows, dead border and
driver chin included, and its front surface sits level with this frame's. Nothing overlaps the
display, so nothing can crop a pixel or press on one — the four M2 screws into the middle plane
hold the board, and this part only frames it and closes the front.

BEZEL_T is therefore two things at once: the frame's thickness, and how far it wraps around the
LCD's edge.
"""
import numpy as np
import trimesh
import button_params as P
from build_body import full_outer, blk, cyl_y, ENG, SEG


def countersink(d, y_face, into, x=0.0, z=0.0):
    """A 90-degree countersink: wide end ON y_face, narrowing INTO the material.

    `into` is +1 when the material lies at greater y than the face (the bezel's front) and -1 when
    it lies at lesser y (the back cover's rear). Getting that sign wrong does NOT fail — it builds
    a cone sitting in free space outside the part, which subtracts nothing and leaves a plain
    through-hole. It did exactly that here until the cone's bounds were printed and checked, and
    the screw-hole loop count could never have caught it.
    """
    depth = d / 2.0                                   # 90 degrees: depth == radius
    m = trimesh.creation.cone(radius=d / 2, height=depth, sections=SEG)
    m.apply_transform(trimesh.transformations.rotation_matrix(-np.pi / 2 * into, (1, 0, 0)))
    m.apply_translation((x, y_face, z))
    return m


def bezel_body():
    return trimesh.boolean.intersection(
        [full_outer(), blk(-P.OUT_X, P.OUT_X, -1.0, P.BEZEL_T, -1.0, P.HEIGHT + 1.0)], engine=ENG)


def opening():
    return blk(P.WIN_X0, P.WIN_X1, -1.0, P.BEZEL_T + 1.0, P.WIN_Z0, P.WIN_Z1)


def screw_holes():
    return trimesh.boolean.union(
        [cyl_y(P.BEZEL_SCREW_D, -1.0, P.BEZEL_T + 1.0, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def countersinks():
    return trimesh.boolean.union(
        [countersink(P.BEZEL_SCREW_HEAD_D, 0.0, +1, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def build_bezel():
    return trimesh.boolean.difference(
        [bezel_body(), opening(), screw_holes(), countersinks()], engine=ENG)


if __name__ == "__main__":
    m = build_bezel()
    sec = m.section(plane_origin=(0, P.BEZEL_T / 2, 0), plane_normal=(0, 1, 0))
    loops = len(sec.discrete) if sec is not None else 0
    assert loops == 6, f"the bezel must show 6 loops (outline + opening + 4 screws); it has {loops}"

    # ⚠️ The loop count above cannot see a countersink — a cone built facing the wrong way subtracts
    # nothing and leaves a tidy plain hole. Measure the volume it removed instead.
    plain = trimesh.boolean.difference([bezel_body(), opening(), screw_holes()], engine=ENG)
    sunk = (plain.volume - m.volume) / 1000.0
    assert sunk > 0.02, f"countersinks removed only {sunk:.4f} cm3 — are the cones facing outward?"
    print(f"  countersinks removed {sunk:.3f} cm3  ok")
    m.export("bezel.stl")
    print(f"  opening    {P.WIN_X1-P.WIN_X0:.2f} x {P.WIN_Z1-P.WIN_Z0:.2f} — the whole glass, "
          f"{P.BEZEL_GAP} clearance")
    print(f"  screws     4x M3 x {P.BEZEL_SCREW_LEN:.0f} countersunk")
    print(f"  bezel.stl  watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
