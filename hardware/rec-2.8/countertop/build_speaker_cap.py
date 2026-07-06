#!/usr/bin/env python3
"""
Build speaker_cap.stl -- the snap-in cap that closes the rear speaker load port.
A face plate covers the port from outside; two cantilever snap arms reach into the
rear cap zone and their ramped hooks click into the two catch recesses in the pocket
side walls (see speaker_cap_catches() in build_shell.py). The cap also backs the
speaker into the pocket.

    python3 build_speaker_cap.py   # -> speaker_cap.stl  (printed plate-down)

Local frame: X = port width, Y = port height, Z = insertion depth (plate at Z 0..CAP_T,
arms extend +Z). Snap fits often need per-printer tuning of CAP_CLR in stand_params.py.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import box, extrude_polygon
from trimesh.boolean import union
from trimesh.transformations import translation_matrix
from shapely.geometry import Polygon
import stand_params as P

PORT_HX = P.SPK_W / 2 + P.SPK_FIT
PORT_HZ = (P.SPK_T + P.SPK_FIT) / 2
ARM_LEN = P.SPK_CAP_ZONE - 0.5

def _lbox(ext, ctr):
    m = box(extents=ext); m.apply_translation(ctr); return m

def _hook(sx):
    """Arrow-profile catch hook: lead-in ramp on the tip AND a lead-OUT ramp on the
    retaining face, so a firm pull cams the arm inward and the cap pops off."""
    z_tip = P.CAP_T + ARM_LEN
    z_apex = z_tip - P.CAP_HOOK_RAMP                     # max outward protrusion
    z_base = z_apex - P.CAP_HOOK_RAMP                    # ramps back to the arm (pry-release)
    x_in = sx * (PORT_HX - P.CAP_CLR)                    # meets the arm outer face
    x_out = sx * (PORT_HX - P.CAP_CLR + P.CAP_HOOK)      # into the catch recess
    tri = Polygon([(x_in, z_base), (x_out, z_apex), (x_in, z_tip)])
    m = extrude_polygon(tri, P.CAP_ARM_Z)               # extruded along +Z (the arm height)
    # remap extrude axes (X=x, Y=z_insertion, Z=arm-height) -> local (x, y, z_insertion)
    Pm = np.array([[1, 0, 0, 0], [0, 0, 1, -P.CAP_ARM_Z / 2],
                   [0, 1, 0, 0], [0, 0, 0, 1]], float)
    m.apply_transform(Pm)
    return m

def build_cap(world=False):
    parts = [_lbox((2 * (PORT_HX + P.CAP_OVER), 2 * (PORT_HZ + P.CAP_OVER), P.CAP_T),
                   (0, 0, P.CAP_T / 2))]                 # face plate
    parts.append(_lbox((P.CAP_PRY_W, P.CAP_PRY_LEN, P.CAP_T),   # bottom-edge pull tab
                       (0, -(PORT_HZ + P.CAP_OVER) - P.CAP_PRY_LEN / 2, P.CAP_T / 2)))
    for sx in (-1, 1):
        parts.append(_lbox((P.CAP_ARM_X, P.CAP_ARM_Z, ARM_LEN),
                           (sx * (PORT_HX - P.CAP_CLR - P.CAP_ARM_X / 2), 0,
                            P.CAP_T + ARM_LEN / 2)))       # snap arm
        parts.append(_hook(sx))                            # hook
    cap = union(parts, engine='manifold')
    if world:
        zc = P.GRILLE_T + (P.SPK_T + P.SPK_FIT) / 2
        M = np.array([[1, 0, 0, 0], [0, 0, -1, P.BACK_Y + P.CAP_T],
                      [0, 1, 0, zc], [0, 0, 0, 1]], float)
        cap.apply_transform(M)
    return cap

if __name__ == "__main__":
    m = build_cap(world=False); m.export("speaker_cap.stl")
    print(f"speaker_cap.stl  watertight={m.is_watertight}  tris={len(m.faces)}  "
          f"bbox={np.round(m.extents,1)}  mm")
