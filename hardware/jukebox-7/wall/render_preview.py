#!/usr/bin/env python3
"""
Render assembly_preview.png (2x2):
  (1) 3D view of the assembled case, from the real meshes
  (2) front elevation -- screen opening, dial + button grid, PCB outline behind
  (3) rear map -- keyholes, USB-C breakout cradle, wall cable hole, bosses
  (4) side section through the control column -- the depth stack

    conda run -n img23d python render_preview.py    # -> assembly_preview.png
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, FancyBboxPatch
import case_params as P
import build_shell as S, build_face as F

shell = S.build_shell()
face = F.build_face()

fig = plt.figure(figsize=(16, 10))
fig.suptitle('sonos-jukebox 7"  —  flush wall case   %.0f x %.0f x %.0f mm'
             % (P.FACE_W, P.FACE_H, P.DEPTH), fontsize=15, weight='bold')

# ---- (1) 3D --------------------------------------------------------------------
a = fig.add_subplot(2, 2, 1, projection='3d'); a.set_title("Assembled (3D)")
light = np.array([-0.4, -0.5, 0.75]); light /= np.linalg.norm(light)


def shaded(m, hexc):
    base = np.array(mcolors.to_rgb(hexc))
    b = 0.55 + 0.45 * np.clip(m.face_normals @ light, 0, 1)
    rgba = np.ones((len(m.faces), 4)); rgba[:, :3] = np.clip(base[None, :] * b[:, None], 0, 1)
    return rgba


for m, c in ((shell, '#e8e6e1'), (face, '#f7f6f4')):
    a.add_collection3d(__import__('mpl_toolkits.mplot3d.art3d', fromlist=['x']).Poly3DCollection(
        m.vertices[m.faces], facecolors=shaded(m, c), edgecolors='none'))
a.set_xlim(0, 230); a.set_ylim(-60, 170); a.set_zlim(-90, 130)
a.set_box_aspect((230, 230, 220)); a.view_init(elev=28, azim=-62); a.axis('off')

# ---- (2) front elevation --------------------------------------------------------
a = fig.add_subplot(2, 2, 2); a.set_title("Front elevation")
a.add_patch(FancyBboxPatch((0, 0), P.FACE_W, P.FACE_H,
                           boxstyle=f"round,pad=0,rounding_size={P.CASE_R}",
                           fc='#f2f1ee', ec='#333', lw=1.4))
a.add_patch(Rectangle((P.PCB_X0, P.PCB_Y0), P.PCB_W, P.PCB_H,
                      fc='none', ec='#3f7d54', ls='--', lw=1.0))
sx0, sy0, sx1, sy1 = F.screen_rect()
a.add_patch(Rectangle((sx0, sy0), sx1 - sx0, sy1 - sy0, fc='#0e0f12', ec='#e8892b', lw=1.4))
a.text((sx0 + sx1) / 2, (sy0 + sy1) / 2,
       "screen 155 x 87\n" + ("MEASURED" if P.AA_MEASURED else "POSITION PROVISIONAL"),
       ha='center', va='center', color='#e8892b', fontsize=9, family='monospace')
a.add_patch(Circle((P.COL_CX, P.DIAL_CY), P.DIAL_D / 2, fc='#cfcbc4', ec='#333', lw=1.2))
for by in P.BTN_ROW_Y:
    for dx in (-P.BTN_PITCH / 2, P.BTN_PITCH / 2):
        a.add_patch(Circle((P.COL_CX + dx, by), P.BTN_D / 2, fc='#dedad3', ec='#333', lw=1.0))
a.plot([P.COL_X0, P.COL_X0], [0, P.FACE_H], color='#999', ls=':', lw=0.9)
a.text(P.COL_CX, 116, f"column {P.COL_W:.1f}", ha='center', fontsize=8,
       color='#666', family='monospace')
a.text(P.FACE_W / 2, -7, "PCB outline dashed — 176.90 x 104.00",
       ha='center', fontsize=8, color='#3f7d54', family='monospace')
a.set_xlim(-10, 240); a.set_ylim(-14, 134); a.set_aspect('equal'); a.axis('off')

# ---- (3) rear map ---------------------------------------------------------------
a = fig.add_subplot(2, 2, 3); a.set_title("Rear (wall side) — mount & power")
a.add_patch(FancyBboxPatch((0, 0), P.FACE_W, P.FACE_H,
                           boxstyle=f"round,pad=0,rounding_size={P.CASE_R}",
                           fc='#eceae6', ec='#333', lw=1.4))
a.add_patch(Rectangle((P.PCB_X0, P.PCB_Y0), P.PCB_W, P.PCB_H,
                      fc='none', ec='#3f7d54', ls='--', lw=1.0))
a.axhline(P.PCB_Y1, color='#bbb', lw=0.8)
for cx in P.KEY_X:
    a.add_patch(Circle((cx, P.KEY_ENTRY_CY), P.KEY_ENTRY_D / 2, fc='#fff', ec='#c0392b', lw=1.5))
    a.add_patch(Rectangle((cx - P.KEY_SLOT_W / 2, P.KEY_ENTRY_CY - P.KEY_DROP),
                          P.KEY_SLOT_W, P.KEY_DROP, fc='#fff', ec='#c0392b', lw=1.5))
a.text(np.mean(P.KEY_X), P.KEY_ENTRY_CY + 6,
       f"2x keyhole — {P.KEY_X[1]-P.KEY_X[0]:.0f} mm apart, {P.KEY_DROP:.1f} mm drop",
       ha='center', fontsize=8, color='#c0392b', family='monospace')
a.add_patch(Rectangle((P.COL_CX - P.KNOB_W / 2, P.DIAL_CY - P.KNOB_H / 2), P.KNOB_W, P.KNOB_H,
                      fc='#3f7d54', alpha=.20, ec='#3f7d54', lw=1.2))
for dx in (-P.KNOB_HOLE_DX / 2, P.KNOB_HOLE_DX / 2):
    for dy in (-P.KNOB_HOLE_DY / 2, P.KNOB_HOLE_DY / 2):
        a.add_patch(Circle((P.COL_CX + dx, P.DIAL_CY + dy), P.KNOB_STANDOFF_D / 2,
                           fc='#d7d3cc', ec='#3f7d54', lw=0.8))
a.text(P.COL_CX, P.DIAL_CY - P.KNOB_H / 2 - 3, "Modulino Knob 41x25.4\n0x76 · 32x16 holes",
       ha='center', va='top', fontsize=7.5, color='#2c6b42', family='monospace')
a.add_patch(Rectangle((P.UC_CX - P.UC_REC_OFF - P.UC_PLATE_T, P.UC_CY - P.UC_H / 2 - 2.5),
                      P.UC_PLATE_T, P.UC_H + 5, fc='#c0392b', alpha=.45, ec='#c0392b'))
a.add_patch(Rectangle((P.UC_CX - P.UC_REC_OFF, P.UC_CY - P.UC_H / 2), P.UC_T, P.UC_H,
                      fc='#c0392b', alpha=.18, ec='#c0392b', ls='--'))
a.add_patch(Rectangle((P.UC_CX - P.PORT_W / 2, P.UC_CY - P.PORT_H / 2), P.PORT_W, P.PORT_H,
                      fc='#fff', ec='#c0392b', lw=1.5))
a.text(P.UC_CX - 8, P.UC_CY - 12, "USB-C breakout\n+ wall cable hole", ha='center',
       va='top', fontsize=8, color='#c0392b', family='monospace')
jx, jy = P.PCB_X0 + P.J10_X, P.PCB_Y0 + P.J10_Y
a.plot([P.UC_CX, jx], [jy, jy], color='#e8892b', lw=2.2)
a.plot([P.UC_CX, P.UC_CX], [P.UC_CY, jy], color='#e8892b', lw=2.2)
a.plot(jx, jy, 'o', color='#e8892b', ms=7)
a.text(jx + 4, jy + 5, "J10  +5V_IN", fontsize=8, color='#e8892b', family='monospace')
hx0, hy0 = P.PCB_X0 + P.HOLE_INSET, P.PCB_Y0 + P.HOLE_INSET
for (x, y) in [(hx0, hy0), (hx0 + P.HOLE_DX, hy0),
               (hx0, hy0 + P.HOLE_DY), (hx0 + P.HOLE_DX, hy0 + P.HOLE_DY)]:
    a.add_patch(Circle((x, y), P.BOSS_OD / 2, fc='#d7d3cc', ec='#666', lw=0.8))
a.set_xlim(-10, 240); a.set_ylim(-14, 134); a.set_aspect('equal'); a.axis('off')

# ---- (4) side section -----------------------------------------------------------
a = fig.add_subplot(2, 2, 4); a.set_title("Side section — depth stack (mm)")
bands = [(0, P.REAR_WALL, '#b9b4ab', 'rear wall 2.5'),
         (P.FLOOR_Z, P.COMP_Z, '#e7e3dc', 'clearance 0.5'),
         (P.COMP_Z, P.PCB_BACK_Z, '#cddcd1', f'rear components {P.REAR_COMP_H} (VERIFY)'),
         (P.PCB_BACK_Z, P.PCB_BACK_Z + P.PCB_T, '#3f7d54', 'PCB 1.65'),
         (P.PCB_BACK_Z + P.PCB_T, P.GLASS_Z, '#cfe3ef', 'display stack'),
         (P.GLASS_Z, P.DEPTH, '#f7f6f4', 'face plate 2.5')]
for z0, z1, c, lab in bands:
    a.add_patch(Rectangle((0, z0), 100, z1 - z0, fc=c, ec='#333', lw=0.9))
    a.text(104, (z0 + z1) / 2, f"{lab}", va='center', fontsize=9, family='monospace')
a.annotate('', xy=(-8, 0), xytext=(-8, P.DEPTH), arrowprops=dict(arrowstyle='<->', color='#333'))
a.text(-12, P.DEPTH / 2, f"{P.DEPTH:.0f} mm", rotation=90, va='center', ha='right',
       fontsize=10, family='monospace', weight='bold')
a.text(50, -2.6, "wall side  ←   |   →  viewer", ha='center', fontsize=8, color='#666')
a.set_xlim(-30, 250); a.set_ylim(-5, 25); a.axis('off')

plt.tight_layout(rect=[0, 0, 1, 0.96])
plt.savefig("assembly_preview.png", dpi=125)
print("assembly_preview.png written")
