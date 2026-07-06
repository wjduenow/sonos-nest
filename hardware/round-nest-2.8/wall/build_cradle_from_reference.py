#!/usr/bin/env python3
"""
Build cradle.stl by GRAFTING the proven Elecrow reference mount (which already
holds the display: centre web, 3xØ4 screw circle on a Ø12 BC, hub clearance)
onto the MAGNETIC pull-off interface that mates wall_plate.stl.

The display-side geometry comes straight from reference_mount.stl. Changes vs the
reference: (1) the board pocket is extended `cup_depth_extra` mm deeper at the
opening, and (2) the wall-facing rim gets inward magnet pads + one anti-rotation
key tab. No bayonet skirt / J-slots. Interface dims: mount_params.py.

    python3 build_cradle_from_reference.py   # -> cradle.stl
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, extrude_polygon
from trimesh.boolean import union, difference, intersection
from shapely.geometry import Polygon
import mount_params as B

REF            = 'reference_mount.stl'
SCREW_HEAD_GAP = 4.0   # mm kept below the web for installed screw heads
WEB_Z          = 3.0   # web's back face in reference-local coordinates
SEG = B.SEG

def cyl(r, h, z0=0.0):
    m = cylinder(radius=r, height=h, sections=SEG); m.apply_translation([0,0,z0+h/2]); return m
def tube(ri, ro, h, z0=0.0):
    return difference([cyl(ro,h,z0), cyl(ri,h+0.4,z0-0.2)], engine='manifold')
def wedge(r, z0, z1, a0, sweep, steps=48):
    a=np.linspace(np.radians(a0),np.radians(a0+sweep),max(2,int(steps*sweep/90)+1))
    pts=[(0,0)]+[(r*np.cos(t), r*np.sin(t)) for t in a]
    m=extrude_polygon(Polygon(pts), height=(z1-z0)); m.apply_translation([0,0,z0]); return m
def arc(r0,r1,z0,z1,a0,sweep):
    return intersection([tube(r0,r1,z1-z0,z0), wedge(r1+5,z0-0.5,z1+0.5,a0,sweep)], engine='manifold')
def rot_z(m, deg):
    m=m.copy(); m.apply_transform(trimesh.transformations.rotation_matrix(np.radians(deg),[0,0,1])); return m

def magnet_pads():
    # inward bosses on the wall-facing rim so a Ø6 magnet has somewhere to live.
    pads=[]
    for i in range(B.magnet_count):
        ang = B.magnet_ang0 + i*360/B.magnet_count
        pads.append(arc(B.pad_ir, B.cup_or+0.1, 0.0, B.pad_h,
                        ang-B.pad_w_deg/2, B.pad_w_deg))
    return pads

def magnet_holes():
    # blind pockets opening at the z=0 mating face; magnets glued in flush.
    pk=[]
    for i in range(B.magnet_count):
        ang = np.radians(B.magnet_ang0 + i*360/B.magnet_count)
        x,y = (B.magnet_bc/2)*np.cos(ang), (B.magnet_bc/2)*np.sin(ang)
        p = cyl(B.mag_pocket_d/2, B.mag_pocket_h+0.1, -0.1)
        p.apply_translation([x,y,0]); pk.append(p)
    return pk

def key_tab():
    # tab on the cup rim that drops into the wall-plate lip slot (anti-rotation)
    return arc(B.cup_or-0.2, B.key_or, 0.0, B.key_h,
               B.key_ang_deg-B.key_w_deg/2, B.key_w_deg)

# --- build ---
# Drop the reference's back floor, keeping ~SCREW_HEAD_GAP of wall below the web.
REF_CUT = WEB_Z - SCREW_HEAD_GAP                 # ~ -1.0 in reference-local coords
ref = trimesh.load(REF)                          # back at z=-10, opening toward +z
ref = ref.slice_plane([0, 0, REF_CUT], [0, 0, 1], cap=True)
ref = ref.copy(); ref.apply_translation([0, 0, -REF_CUT])    # rim -> z=0
top = ref.bounds[1, 2]                            # cup opening (room-side)

parts = [ref]
# deeper board pocket: extend the barrel wall at the opening
if B.cup_depth_extra > 0:
    parts.append(tube(B.barrel_ir, B.barrel_or, B.cup_depth_extra, top))
parts += magnet_pads()
if B.anti_rotation_key:
    parts.append(key_tab())
body = union(parts, engine='manifold')

cradle = difference([body] + magnet_holes(), engine='manifold')

if __name__ == "__main__":
    cradle.export('cradle.stl')
    print(f'cradle.stl  watertight={cradle.is_watertight}  tris={len(cradle.faces)}  '
          f'bbox={np.round(cradle.extents,1)}  height={round(cradle.extents[2],1)}mm')
