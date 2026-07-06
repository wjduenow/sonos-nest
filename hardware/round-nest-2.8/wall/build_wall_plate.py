#!/usr/bin/env python3
"""
Build wall_plate.stl -- the part that screws to the wall and forms the MAGNETIC
mount interface: a shallow lip the cradle's cup nests into (centring + shear),
disc-magnet pockets that pull the cradle on, and one anti-rotation key slot.
Interface dims come from mount_params.py (shared with the cradle so they match).

Kept from the proven plate: the 2-screw wall pattern (countersunk, on a bolt
circle inside the lip), the centre cable pass-through, and the edge cable notch.

    python3 build_wall_plate.py   # -> wall_plate.stl
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, box, extrude_polygon, cone
from trimesh.boolean import union, difference, intersection
from shapely.geometry import Polygon
import mount_params as B

# --- wall-plate specific params ---
disc_thk    = 4.0
screw_count = 2       # 2 = horizontal pair; bump to 4 for stud + anchors
screw_bc    = 36.0    # bolt circle for wall screws -- INSIDE the lip
screw_d     = 4.4     # clearance for ~#8 / M4 screw shank
screw_head  = 8.6     # countersink head diameter (flat head)
side_notch  = True    # slot in the disc edge for a surface-run cable

SEG = B.SEG

def cyl(r, h, z0=0.0):
    m = cylinder(radius=r, height=h, sections=SEG); m.apply_translation([0,0,z0+h/2]); return m
def tube(ri, ro, h, z0=0.0):
    return difference([cyl(ro,h,z0), cyl(ri,h+0.4,z0-0.2)], engine='manifold')
def arc(r0,r1,z0,z1,a0,sweep,steps=48):
    a=np.linspace(np.radians(a0),np.radians(a0+sweep),max(2,int(steps*sweep/90)+1))
    pts=[(0,0)]+[tuple((r1+5)*np.array([np.cos(t),np.sin(t)])) for t in a]
    w=extrude_polygon(Polygon(pts),height=(z1-z0)+1); w.apply_translation([0,0,z0-0.5])
    return intersection([tube(r0,r1,z1-z0,z0), w], engine='manifold')
def rot_z(m, deg):
    m=m.copy(); m.apply_transform(trimesh.transformations.rotation_matrix(np.radians(deg),[0,0,1])); return m

def lip_ring():
    # shallow ring the cup nests into; centres the unit + carries its weight.
    ring = tube(B.lip_ir, B.lip_or, B.lip_h, disc_thk)
    if B.anti_rotation_key:
        # cut a slot through the lip so the cradle's key tab can drop in
        slot = arc(B.lip_ir-0.5, B.lip_or+1, disc_thk-0.5, disc_thk+B.lip_h+0.5,
                   B.key_ang_deg-B.key_slot_deg/2, B.key_slot_deg)
        ring = difference([ring, slot], engine='manifold')
    return ring

def magnet_pockets():
    # blind pockets opening at the mating face (top); magnets glued in flush.
    pk=[]
    for i in range(B.magnet_count):
        ang = np.radians(B.magnet_ang0 + i*360/B.magnet_count)
        x,y = (B.magnet_bc/2)*np.cos(ang), (B.magnet_bc/2)*np.sin(ang)
        p = cyl(B.mag_pocket_d/2, B.mag_pocket_h+0.1, disc_thk-B.mag_pocket_h)
        p.apply_translation([x,y,0]); pk.append(p)
    return pk

def screw_hole():
    shank = cyl(screw_d/2, disc_thk+0.4, -0.2)
    csk_h = (screw_head-screw_d)/2 + 0.6
    c = cone(radius=screw_head/2, height=csk_h, sections=SEG)
    c.apply_transform(trimesh.transformations.rotation_matrix(np.pi,[1,0,0]))
    c.apply_translation([0,0,disc_thk+0.01])
    return union([shank,c], engine='manifold')

def build_plate():
    body = union([cyl(B.plate_dia/2, disc_thk), lip_ring()], engine='manifold')
    cuts = [cyl(B.cable_hole/2, disc_thk+B.lip_h+1, -0.1)]
    cuts += magnet_pockets()
    for i in range(screw_count):   # screws at 0/180; cable notch at 90 -> no clash
        h = screw_hole(); h.apply_translation([screw_bc/2,0,0])
        cuts.append(rot_z(h, i*360/screw_count))
    if side_notch:
        n = box(extents=(B.plate_dia/2+2, B.cable_hole, disc_thk+0.4))
        n.apply_translation([(B.plate_dia/2+2)/2, 0, disc_thk/2-0.1])
        cuts.append(rot_z(n, 90))
    return difference([body]+cuts, engine='manifold')

if __name__ == "__main__":
    m = build_plate(); m.export("wall_plate.stl")
    print(f"wall_plate.stl  watertight={m.is_watertight}  tris={len(m.faces)}  "
          f"bbox={np.round(m.extents,1)}  height={round(m.extents[2],1)}mm")
