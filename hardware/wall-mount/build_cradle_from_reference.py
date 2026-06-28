#!/usr/bin/env python3
"""
Build cradle.stl by GRAFTING the proven Elecrow reference mount (which already
holds the display: Ø58 cup, centre web, 3xØ4 screw circle, hub clearance) onto
the twist-lock bayonet skirt that mates wall_plate.stl.

The display-side geometry comes straight from reference_mount.stl (unchanged).
Bayonet dims come from bayonet_params.py (shared with the wall plate).

    python3 build_cradle_from_reference.py   # -> cradle.stl
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, extrude_polygon
from trimesh.boolean import union, difference, intersection
from shapely.geometry import Polygon
import bayonet_params as B

REF       = 'reference_mount.stl'
# The bayonet skirt is CO-LOCATED with the cup's own lower wall (it just thickens
# that wall to 3 mm and the J-slots are cut into it) -- so there's no separate
# standoff below the screw web. Only a few mm is kept behind the web for the
# installed screw heads. This roughly halves the structure behind the display.
skirt_h        = 9.0   # height of the bayonet band (cut into the cup's lower wall)
SCREW_HEAD_GAP = 4.0   # mm kept below the web for installed screw heads
WEB_Z          = 3.0   # web's back face in reference-local coordinates
# (The cable exits the 5V-IN edge connector with room through the reference's
#  existing web openings, so no dedicated cable channel is needed.)
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

def jslot():
    r0,r1 = B.cradle_ir-0.3, B.cradle_or+0.3
    slot_ang = B.lug_w_deg+4; axial_top = B.lug_z+B.slot_w/2+0.6
    return union([arc(r0,r1,-0.2,axial_top,-slot_ang/2,slot_ang),
                  arc(r0,r1,B.lug_z-B.slot_w/2,B.lug_z+B.slot_w/2,-slot_ang/2,slot_ang+B.twist_deg)],
                 engine='manifold')

# --- build ---
# Drop the reference's back floor + excess lower shell, keeping only ~SCREW_HEAD_GAP
# of wall below the web. The kept lower wall + the skirt band (co-located) form the
# bayonet; the rest of the cup holds the display. No separate standoff, no bridge.
REF_CUT = WEB_Z - SCREW_HEAD_GAP                 # ~ -1.0 in reference-local coords
ref = trimesh.load(REF)                          # back at z=-10, opening toward +z
ref = ref.slice_plane([0, 0, REF_CUT], [0, 0, 1], cap=True)
ref = ref.copy(); ref.apply_translation([0, 0, -REF_CUT])    # cup bottom -> z=0

# bayonet band: thickens the cup's lower wall to 3 mm; J-slots cut into it
skirt = tube(B.cradle_ir, B.cradle_or, skirt_h, 0)
body  = union([skirt, ref], engine='manifold')

cuts  = [rot_z(jslot(), i*360/B.lug_count) for i in range(B.lug_count)]
cradle = difference([body]+cuts, engine='manifold')

if __name__ == "__main__":
    cradle.export('cradle.stl')
    print(f'cradle.stl  watertight={cradle.is_watertight}  tris={len(cradle.faces)}  '
          f'bbox={np.round(cradle.extents,1)}  height={round(cradle.extents[2],1)}mm')
