"""button-v3 back cover — the rear plate.

Comes off to reach the four M2 screws that hold the board to the middle plane. That is its only
functional job: with it removed there is 10.5 mm of open cavity behind the plane, which is what
makes the board joint reachable at all.

Screws into the SAME corner bosses the bezel uses, from the other end — one boss, a pilot drilled
in from each side.
"""
import numpy as np
import trimesh
import button_params as P
from build_body import full_outer, blk, cyl_y, ENG
from build_bezel import countersink


def back_body():
    return trimesh.boolean.intersection(
        [full_outer(), blk(-P.OUT_X, P.OUT_X, P.BODY_Y1, P.OUT_Y + 1.0, -1.0, P.HEIGHT + 1.0)],
        engine=ENG)


def screw_holes():
    return trimesh.boolean.union(
        [cyl_y(P.BEZEL_SCREW_D, P.BODY_Y1 - 1.0, P.OUT_Y + 1.0, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def countersinks():
    """Sunk from the REAR face, so they open the opposite way to the bezel's — hence into=-1."""
    return trimesh.boolean.union(
        [countersink(P.BEZEL_SCREW_HEAD_D, P.OUT_Y, -1, x=sx * P.BOSS_X, z=z)
         for sx in (-1, 1) for z in P.BOSS_ZS], engine=ENG)


def build_back():
    return trimesh.boolean.difference([back_body(), screw_holes(), countersinks()], engine=ENG)


if __name__ == "__main__":
    from build_body import build_body
    from build_bezel import build_bezel
    m = build_back()
    plain = trimesh.boolean.difference([back_body(), screw_holes()], engine=ENG)
    sunk = (plain.volume - m.volume) / 1000.0
    assert sunk > 0.02, f"countersinks removed only {sunk:.4f} cm3 — are the cones facing outward?"
    print(f"  countersinks removed {sunk:.3f} cm3  ok")

    # ⚠️ Per-part checks cannot catch an assembly fault — this caught a real one earlier, where two
    # parts were each watertight and correct and shared 0.064 cm3 of space.
    body, bez = build_body(), build_bezel()
    for a, b, na, nb in ((body, bez, "body", "bezel"), (body, m, "body", "back"),
                         (bez, m, "bezel", "back")):
        v = trimesh.boolean.intersection([a, b], engine=ENG).volume / 1000.0
        assert v < 1e-3, f"{na} and {nb} occupy the same space: {v:.4f} cm3"
        print(f"  interference  {na:5} n {nb:5} = {v:.5f} cm3  ok")

    m.export("back.stl")
    print(f"  screws     4x M3 x {P.BACK_SCREW_LEN:.0f}, into the same bosses from the rear")
    print(f"  back.stl   watertight={m.is_watertight} winding={m.is_winding_consistent} "
          f"volume={m.volume/1000:.2f}cm3 tris={len(m.faces)}")
