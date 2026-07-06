#!/usr/bin/env python3
"""
Render assembly_preview.png: wall-plate face, cradle wall-facing face, and a
side cross-section through the magnets showing the lip nest + deeper pocket.
Schematic top views are drawn from mount_params (exact); the section is sliced
from the real meshes.

    python3 render_preview.py   # -> assembly_preview.png
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Wedge
import mount_params as B, build_wall_plate as W, build_cradle_from_reference as C

plate=W.build_plate(); cradle=C.cradle; DISC=W.disc_thk
SCREWS=[(-5.05,-0.04),(3.98,5.45),(3.98,-5.02)]   # display screws (Ø12 BC)
mag_xy=[((B.magnet_bc/2)*np.cos(np.radians(a)),(B.magnet_bc/2)*np.sin(np.radians(a)))
        for a in (B.magnet_ang0+i*360/B.magnet_count for i in range(B.magnet_count))]

fig,ax=plt.subplots(1,3,figsize=(15,5.2)); fig.suptitle(
    "Sonos knob wall mount — magnetic pull-off",fontsize=14,weight='bold')

# --- (1) wall plate, looking at the mating face ---
a=ax[0]; a.set_title("Wall plate (face that meets the cradle)")
a.add_patch(Circle((0,0),B.plate_dia/2,fill=False,lw=1.2))
a.add_patch(Wedge((0,0),B.lip_or,0,360,width=B.lip_or-B.lip_ir,fc='#cfe0f5',ec='k',lw=0.8))  # lip ring
# key slot (gap in the lip)
a.add_patch(Wedge((0,0),B.lip_or+0.3,B.key_ang_deg-B.key_slot_deg/2,B.key_ang_deg+B.key_slot_deg/2,
                  width=B.lip_or-B.lip_ir+0.6,fc='white',ec='r',lw=1.0))
a.text(0,-B.lip_or-2,'key slot',color='r',ha='center',va='top',fontsize=8)
a.add_patch(Circle((0,0),B.cable_hole/2,fc='#dde',ec='b',lw=1)); a.text(0,0,'cable',ha='center',va='center',fontsize=7,color='b')
for x in (-W.screw_bc/2,W.screw_bc/2):
    a.add_patch(Circle((x,0),W.screw_head/2,fill=False,ec='#888',lw=0.8))
    a.add_patch(Circle((x,0),W.screw_d/2,fc='#bbb',ec='k',lw=0.8))
a.text(0,W.screw_bc/2-1,'wall screws (countersunk)',ha='center',fontsize=7,color='#555')
for x,y in mag_xy:
    a.add_patch(Circle((x,y),B.magnet_d/2,fc='#e8b',ec='k',lw=0.8))
a.text(mag_xy[0][0],mag_xy[0][1],'mag',ha='center',va='center',fontsize=6)

# --- (2) cradle, looking at the wall-facing face ---
a=ax[1]; a.set_title("Cradle (wall-facing face: pads + key)")
a.add_patch(Circle((0,0),B.cup_or,fill=False,lw=1.2))                       # cup outer
a.add_patch(Wedge((0,0),B.cup_or,0,360,width=B.cup_or-B.cup_ir,fc='#eee',ec='k',lw=0.6))  # rim wall
# magnet pads (inward bosses) + magnets
for (x,y),ang in zip(mag_xy,(B.magnet_ang0+i*360/B.magnet_count for i in range(B.magnet_count))):
    a.add_patch(Wedge((0,0),B.cup_or,ang-B.pad_w_deg/2,ang+B.pad_w_deg/2,width=B.cup_or-B.pad_ir,fc='#d8e8d8',ec='k',lw=0.6))
    a.add_patch(Circle((x,y),B.magnet_d/2,fc='#e8b',ec='k',lw=0.8))
# key tab
a.add_patch(Wedge((0,0),B.key_or,B.key_ang_deg-B.key_w_deg/2,B.key_ang_deg+B.key_w_deg/2,width=B.key_or-B.cup_or+2,fc='#f5cfcf',ec='r',lw=1.0))
a.text(0,-B.key_or-2,'key tab',color='r',ha='center',va='top',fontsize=8)
a.add_patch(Circle((0,0),12/2,fill=False,ec='g',lw=0.8,ls='--'))           # Ø12 BC
for dx,dy in SCREWS: a.add_patch(Circle((dx,dy),2,fc='#bbb',ec='k',lw=0.6))
a.text(0,8,'display screws',ha='center',fontsize=7,color='#555')

for a in ax[:2]:
    a.set_aspect('equal'); a.set_xlim(-38,38); a.set_ylim(-38,38); a.axis('off')

# --- (3) side cross-section through the magnets (rotate so a magnet is at +X, cut y=0) ---
a=ax[2]; a.set_title("Section through magnets (assembled)")
def sect(m,zoff):
    mm=m.copy(); mm.apply_translation([0,0,zoff])
    mm.apply_transform(trimesh.transformations.rotation_matrix(np.radians(-B.magnet_ang0),[0,0,1]))
    s=mm.section(plane_origin=[0,0,0],plane_normal=[0,1,0])
    return s.to_planar()[0] if s else None
for m,zoff,fc,lbl in ((plate,0,'#cfe0f5','wall plate'),(cradle,DISC,'#d8e8d8','cradle')):
    p=sect(m,zoff)
    if p:
        for poly in p.polygons_full:
            xy=np.array(poly.exterior.coords); a.fill(xy[:,0],xy[:,1],fc=fc,ec='k',lw=0.7,zorder=1)
a.axhline(DISC,color='r',ls=':',lw=0.8); a.text(36,DISC+0.3,'mating plane (magnets meet)',color='r',fontsize=7,va='bottom',ha='right')
a.annotate('+%g mm deeper\npocket'%B.cup_depth_extra,xy=(0,DISC+22),xytext=(14,DISC+22),fontsize=8,va='center',
           arrowprops=dict(arrowstyle='->',color='k'))
a.set_aspect('equal'); a.set_xlim(-40,40); a.set_ylim(-2,30); a.axis('off')
a.text(0,-1.5,'wall side ↓     room side ↑',ha='center',fontsize=8,color='#555')

plt.tight_layout(rect=(0,0,1,0.95)); plt.savefig('assembly_preview.png',dpi=110); print("wrote assembly_preview.png")