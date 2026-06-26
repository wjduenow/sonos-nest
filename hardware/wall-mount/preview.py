#!/usr/bin/env python3
"""Render preview PNGs of the STLs (headless, matplotlib)."""
import numpy as np, trimesh, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

def show(mesh, ax, elev, azim, title):
    tris = mesh.vertices[mesh.faces]
    # shade by face normal vs a light direction
    n = mesh.face_normals
    light = np.array([0.3, 0.4, 0.85]); light = light/np.linalg.norm(light)
    shade = 0.35 + 0.65*np.clip(n.dot(light), 0, 1)
    colors = np.column_stack([0.40*shade, 0.55*shade, 0.95*shade, np.ones(len(shade))])
    pc = Poly3DCollection(tris, facecolors=colors, edgecolors=(0,0,0,0.06), linewidths=0.2)
    ax.add_collection3d(pc)
    b = mesh.bounds; ctr = mesh.centroid; r = (b[1]-b[0]).max()/2
    ax.set_xlim(ctr[0]-r, ctr[0]+r); ax.set_ylim(ctr[1]-r, ctr[1]+r); ax.set_zlim(ctr[2]-r, ctr[2]+r)
    ax.set_box_aspect((1,1,1)); ax.view_init(elev=elev, azim=azim)
    ax.set_title(title, fontsize=10); ax.set_axis_off()

specs = [
    ("wall_plate.stl", [(35, -60, "Wall plate — front (ring + lugs face the device)"),
                        (-35, -60, "Wall plate — back (screw countersinks against wall)")]),
    ("cradle.stl",     [(30, -60, "Cradle — front (device drops in, lip retains it)"),
                        (20, 120, "Cradle — back (J-slots + USB-C cutout)")]),
]
for fname, views in specs:
    m = trimesh.load(fname)
    fig = plt.figure(figsize=(11, 5.2))
    for i,(elev,azim,t) in enumerate(views):
        ax = fig.add_subplot(1, 2, i+1, projection="3d")
        show(m, ax, elev, azim, t)
    out = fname.replace(".stl", "_preview.png")
    fig.tight_layout(); fig.savefig(out, dpi=120); plt.close(fig)
    print("wrote", out)
