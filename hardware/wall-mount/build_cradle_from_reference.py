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
skirt_h   = 9.0      # cradle skirt (a bit taller than ring_h to cover the slot)
bridge_thk = 3.0     # disc bridging the Ø86 skirt to the reference's Ø62 back
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
# The reference has its OWN mounting base (back floor + lower shell) BELOW the
# screw web -- redundant now that our bayonet skirt does that job. Trim it off
# and keep only the display-holding top (web + screws + body-locating ring).
REF_CUT = 2.5     # keep reference geometry at z >= this (just under the web)
ref = trimesh.load(REF)                          # back at z=-10, opening toward +z
ref = ref.slice_plane([0, 0, REF_CUT], [0, 0, 1], cap=True)   # drop redundant base
shift = (skirt_h + bridge_thk) - ref.bounds[0][2]
ref = ref.copy(); ref.apply_translation([0, 0, shift])

skirt = tube(B.cradle_ir, B.cradle_or, skirt_h, 0)        # twist-lock skirt
# OPEN ring (not a solid disc) joining the skirt to the reference's rim (now
# the same Ø61). The centre stays open so you can reach the screw holes from the
# back and route the cable. Inner radius overlaps the reference rim (~R29).
bridge_ir = 22.0
base  = tube(bridge_ir, B.cradle_or, bridge_thk, skirt_h)
body  = union([skirt, base, ref], engine='manifold')

cuts  = [rot_z(jslot(), i*360/B.lug_count) for i in range(B.lug_count)]
cradle = difference([body]+cuts, engine='manifold')

if __name__ == "__main__":
    cradle.export('cradle.stl')
    print(f'cradle.stl  watertight={cradle.is_watertight}  tris={len(cradle.faces)}  '
          f'bbox={np.round(cradle.extents,1)}  height={round(cradle.extents[2],1)}mm')
