#!/usr/bin/env python3
"""
Render assembly_preview.png (2x2):
  (1) 3D view of the assembled stand (shell + bezel), from the real meshes
  (2) side cross-section sliced through a mount boss (x = +39) -> shows how the
      board seats on a boss and the bezel caps the glass
  (3) front elevation (what you see): screen opening + board features
  (4) back-face map: RESET pin hole + microSD window (ESTIMATED -- verify!)

    python3 render_preview.py   # -> assembly_preview.png
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import stand_params as P
import build_shell as S, build_bezel as B

sT, cT = np.sin(np.radians(P.TILT_DEG)), np.cos(np.radians(P.TILT_DEG))
_M = S._M
def w(x, y, z): return (_M @ np.array([x, y, z, 1.0]))[:3]

shell = S.build_shell()
bezel = B.build_bezel(world=True)

fig = plt.figure(figsize=(15, 10))
fig.suptitle("ES3C28P 2.8\" ESP32-S3 nightstand stand  —  landscape, 20° recline",
             fontsize=14, weight='bold')

# ---- (1) 3D view ----------------------------------------------------------------
a = fig.add_subplot(2, 2, 1, projection='3d'); a.set_title("Assembled (3D, board shown)")
light = np.array([-0.4, -0.5, 0.75]); light = light / np.linalg.norm(light)
def shaded(m, hexc):
    base = np.array(mcolors.to_rgb(hexc))
    b = 0.55 + 0.45 * np.clip(m.face_normals @ light, 0, 1)      # per-face brightness
    rgba = np.ones((len(m.faces), 4)); rgba[:, :3] = np.clip(base[None, :] * b[:, None], 0, 1)
    return rgba
# a stand-in board (PCB + glass + screen) so the preview reads correctly
def lblock(ext, ctr):
    m = trimesh.creation.box(extents=ext); m.apply_translation(ctr)
    m.apply_transform(_M); return m
pcb   = lblock((P.PCB_W, P.PCB_H, P.PCB_T), (0, 0, -P.PCB_T/2))
glass = lblock((P.GLASS_W, P.GLASS_H, P.GLASS_PROUD), (0, 0, P.GLASS_PROUD/2))
scr   = lblock((P.AA_W, P.AA_H, 0.2), (0, 0, P.GLASS_PROUD+0.11))
for m, hexc in ((shell, '#93b4dd'), (pcb, '#2a6b32'), (glass, '#111417'),
                (scr, '#2d6ae0'), (bezel, '#e0b878')):
    a.add_collection3d(Poly3DCollection(m.triangles, facecolors=shaded(m, hexc),
                                        edgecolors='none', linewidths=0))
a.set_xlim(-50, 50); a.set_ylim(-5, 55); a.set_zlim(0, 70)
a.set_box_aspect((100, 60, 70)); a.view_init(elev=18, azim=-72)
a.set_axis_off()

# ---- (2) side section through a mount boss (x = +39) ----------------------------
a = fig.add_subplot(2, 2, 2); a.set_title("Side section through a mount boss")
for m, fc, lbl in ((shell, '#cfe0f5', 'shell'), (bezel, '#f3d9b1', 'bezel')):
    sec = m.section(plane_origin=[P.HOLE_DX/2, 0, 0], plane_normal=[1, 0, 0])
    if sec is None: continue
    planar, _ = sec.to_planar()
    for poly in planar.polygons_full:
        xy = np.array(poly.exterior.coords)
        a.fill(xy[:, 0], xy[:, 1], fc=fc, ec='k', lw=0.7, zorder=2, label=lbl); lbl = None
def seg(p, q, **kw): a.plot([p[1], q[1]], [p[2], q[2]], **kw)
seg(w(P.HOLE_DX/2, -P.PCB_H/2, 0), w(P.HOLE_DX/2, P.PCB_H/2, 0),
    color='#159', lw=2.4, zorder=4, label='PCB')
seg(w(P.HOLE_DX/2, -P.GLASS_H/2, P.GLASS_PROUD), w(P.HOLE_DX/2, P.GLASS_H/2, P.GLASS_PROUD),
    color='#2a2', lw=2.0, zorder=4, label='glass')
a.axhline(0, color='#777', lw=3, zorder=0)
a.text(P.BACK_Y, -1.6, 'nightstand', fontsize=7, color='#555', ha='right', va='top')
# cable route (schematic, projected into y-z): plug -> down the -X face -> back -> rear
pu = w(-P.PCB_W/2, P.USB_Y, -P.PCB_T/2)
route = [(pu[1]-6, pu[2]), (pu[1], pu[2]), (pu[1]*0.3, 2.5), (P.BACK_Y+3, 2.5)]
a.plot([p[0] for p in route], [p[1] for p in route], color='#e07000', lw=2.0,
       ls='--', zorder=6)
a.plot(pu[1]-6, pu[2], marker='o', color='#e07000', ms=5, zorder=6)
for cy in P.CLIP_YS:                                    # snap-in clip positions
    a.plot(cy, 2.5, marker='v', color='#0a0', ms=6, zorder=7)
a.text(P.CLIP_YS[1], -2.4, 'clips', color='#0a0', fontsize=7, ha='center', va='top')
a.text(P.BACK_Y-2, 5.0, 'cable: down & back → rear', color='#e07000', fontsize=7, ha='right')
a.set_aspect('equal'); a.set_xlim(-10, P.BACK_Y+6); a.set_ylim(-6, 70); a.axis('off')
a.legend(loc='upper right', fontsize=7, framealpha=0.9)

# ---- (3) front elevation --------------------------------------------------------
a = fig.add_subplot(2, 2, 3); a.set_title("Front (what you see)")
def rect(cx, cy, ww, hh, **kw): a.add_patch(Rectangle((cx-ww/2, cy-hh/2), ww, hh, **kw))
rect(0, 0, P.W_OUT, P.FACE_LEN, fc='#eef2f7', ec='#888', lw=1.0)
rect(0, 0, P.PCB_W, P.PCB_H, fc='none', ec='#159', lw=1.4)
a.text(0, P.PCB_H/2+2, 'PCB 86×50', color='#159', fontsize=7, ha='center')
rect(0, 0, P.GLASS_W, P.GLASS_H, fc='#dff0df', ec='#2a2', lw=0.8)
rect(0, 0, P.VA_W, P.VA_H, fc='#111', ec='#111')
rect(0, 0, P.VA_W+2*P.OPEN_MARGIN, P.VA_H+2*P.OPEN_MARGIN, fc='none', ec='#fa0', lw=1.6, ls='--')
a.text(0, 0, 'screen\nopening', color='#fa0', fontsize=7, ha='center', va='center')
for sx in (-1, 1):
    for sy in (-1, 1):
        a.add_patch(Circle((sx*P.HOLE_DX/2, sy*P.HOLE_DY/2), P.HOLE_D/2, fc='#bbb', ec='k', lw=0.6))
        a.add_patch(Circle((sx*P.POST_X, sy*P.POST_Y), 3.0, fc='none', ec='#a0f', lw=1.0))
a.text(0, P.POST_Y+2.4, 'board screws ● / bezel screws ○ (into rim)', color='#555', fontsize=6, ha='center')
a.plot([-P.PCB_W/2, -P.PCB_W/2], [P.USB_Y-P.USB_SLOT_Y/2, P.USB_Y+P.USB_SLOT_Y/2], color='#c33', lw=3)
a.annotate('USB-C', xy=(-P.PCB_W/2, P.USB_Y), xytext=(-P.PCB_W/2-10, P.USB_Y),
           color='#c33', fontsize=8, ha='right', va='center', arrowprops=dict(arrowstyle='->', color='#c33'))
a.add_patch(Circle((P.MIC_X, P.MIC_Y), P.MIC_HOLE_D/2+0.6, fc='#0aa', ec='#066', lw=1.0))
a.annotate('mic hole\n(bezel)', xy=(P.MIC_X, P.MIC_Y), xytext=(P.MIC_X+11, P.MIC_Y),
           color='#088', fontsize=7, ha='left', va='center', arrowprops=dict(arrowstyle='->', color='#088'))
a.set_aspect('equal'); a.set_xlim(-56, 56); a.set_ylim(-40, 40); a.axis('off')

# ---- (4) back map ---------------------------------------------------------------
a = fig.add_subplot(2, 2, 4); a.set_title("Back — access holes (VERIFY positions!)")
rect(0, 0, P.PCB_W, P.PCB_H, fc='#f7f2ee', ec='#888', lw=1.2)
a.text(0, P.PCB_H/2+2, 'board rear', color='#555', fontsize=7, ha='center')
a.add_patch(Circle((P.RESET_X, P.RESET_Y), P.RESET_PIN_D/2+1.2, fc='#fdd', ec='#c33', lw=1.2))
a.add_patch(Circle((P.RESET_X, P.RESET_Y), P.RESET_PIN_D/2, fc='#c33', ec='k', lw=0.5))
a.text(P.RESET_X, P.RESET_Y-3.5, 'RESET pin', color='#c33', fontsize=7, ha='center', va='top')
a.add_patch(Rectangle((P.SD_X-P.SD_WIN_X/2, P.SD_Y-P.SD_WIN_Z/2), P.SD_WIN_X, P.SD_WIN_Z,
                      fc='#ddf', ec='#33c', lw=1.2))
a.text(P.SD_X, P.SD_Y, 'microSD\naccess', color='#33c', fontsize=7, ha='center', va='center')
a.plot([-P.PCB_W/2, -P.PCB_W/2], [-P.PCB_H/2, P.PCB_H/2], color='#c33', lw=2.5)
a.text(-P.PCB_W/2-1.5, 0, 'USB-C / button edge', color='#c33', fontsize=6, ha='right', va='center', rotation=90)
a.set_aspect('equal'); a.set_xlim(-56, 56); a.set_ylim(-40, 40); a.axis('off')

plt.tight_layout(rect=(0, 0, 1, 0.96))
plt.savefig('assembly_preview.png', dpi=120)
print("wrote assembly_preview.png")
