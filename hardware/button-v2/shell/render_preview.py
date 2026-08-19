#!/usr/bin/env python3
"""
Render render_preview.png (2x2):
  (1) exploded 3D of the assembly, from the real meshes
  (2) the Z stack -- THE drawing: the board sits flat OVER the button's back, and the lid rib
      is what holds it there, because this board has no mounting holes
  (3) bottom view -- the button face, what the user reaches up and presses
  (4) interior plan: the STEP-verified board features, what the rib bears on, and where the
      antenna and the harness live

    python3 render_preview.py   # -> render_preview.png
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, RegularPolygon
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import button_params as P
import build_shell as S, build_lid as L

shell = S.build_shell()
lid = L.build_lid(world=True)

fig = plt.figure(figsize=(15, 10.5))
fig.suptitle(f"button-v2 — XIAO ESP32S3 + FLM12-FJ-6, tape or keyhole slots   "
             f"({P.OUT_X:.1f} × {P.OUT_Y:.1f} × {P.HEIGHT:.2f} mm, "
             f"{P.OUT_X*P.OUT_Y*P.HEIGHT/1000:.1f} cm³ — cam-button is 57.1)",
             fontsize=14, weight='bold')

C_SHELL, C_LID, C_PCB, C_BTN = '#93b4dd', '#e0b878', '#2a6b32', '#b8bcc4'


def blk(ext, ctr):
    m = trimesh.creation.box(extents=ext); m.apply_translation(ctr); return m


# stand-in board + button so the preview reads as an assembly
pcb = blk((P.PCB_W, P.PCB_L, P.PCB_T), (0, 0, P.PCB_BACK_Z + P.PCB_T / 2))
btn_body = trimesh.creation.cylinder(radius=P.BUTTON_BODY_D / 2,
                                     height=P.BUTTON_INSIDE_T, sections=64)
btn_body.apply_translation([P.BUTTON_CX, P.BUTTON_CY, P.BUTTON_INSIDE_T / 2])
btn_head = trimesh.creation.cylinder(radius=P.BUTTON_HEAD_D / 2, height=P.BUTTON_HEAD_T,
                                     sections=64)
btn_head.apply_translation([P.BUTTON_CX, P.BUTTON_CY, -P.BUTTON_HEAD_T / 2])

# ---- (1) exploded 3D --------------------------------------------------------------
a = fig.add_subplot(2, 2, 1, projection='3d')
a.set_title("Exploded — assembly order (button face DOWN, tape face UP)")
light = np.array([-0.4, -0.5, 0.75]); light /= np.linalg.norm(light)


def shaded(m, hexc):
    base = np.array(mcolors.to_rgb(hexc))
    b = 0.55 + 0.45 * np.clip(m.face_normals @ light, 0, 1)
    rgba = np.ones((len(m.faces), 4)); rgba[:, :3] = np.clip(base[None, :] * b[:, None], 0, 1)
    return rgba


# matplotlib's 3D has no depth sorting, so a closed box renders as a mess; pulling the parts
# apart is both clearer and honest about the build order.
EXPLODE = ((shell, C_SHELL, 0.0), (btn_body, C_BTN, -20.0), (btn_head, '#6e7480', -20.0),
           (pcb, C_PCB, 12.0), (lid, C_LID, 26.0))
for m, hexc, dz in EXPLODE:
    mm = m.copy(); mm.apply_translation([0, 0, dz])
    a.add_collection3d(Poly3DCollection(mm.triangles, facecolors=shaded(mm, hexc),
                                        edgecolors='none', linewidths=0))
a.set_xlim(-25, 25); a.set_ylim(-24, 24); a.set_zlim(-24, 56)
a.set_box_aspect((50, 48, 80))
a.view_init(elev=14, azim=-66)
a.set_axis_off()
for txt, fy, col in (
        ("4. lid — TAPE FACE or keyholes  (4 × M3×10 flat, flush)", 0.92, '#8a6a2a'),
        ("   ...its rib presses the board down. NO BOARD SCREWS.", 0.83, '#8a6a2a'),
        ("3. board DROPS onto four ledges", 0.66, '#063'),
        ("2. shell", 0.48, '#456'),
        ("1. button up through the bore, M12 nut inside", 0.10, '#333')):
    a.text2D(0.0, fy, txt, transform=a.transAxes, color=col, fontsize=7, ha='left',
             weight='bold')

# ---- (2) the Z stack --------------------------------------------------------------
a = fig.add_subplot(2, 2, 2)
a.set_title("The Z stack — board sits OVER the button; the lid rib is the hold-down")


def hband(z0, z1, x0, x1, fc, ec, label=None, hatch=None, z=2):
    a.add_patch(Rectangle((min(x0, x1), z0), abs(x1 - x0), z1 - z0, fc=fc, ec=ec, lw=1.0,
                          zorder=z, label=label, hatch=hatch))


hband(0, P.BOTTOM_WALL, -P.OUT_X / 2, P.OUT_X / 2, '#cfe0f5', '#456', 'shell')
hband(P.CEIL_Z, P.HEIGHT, -P.OUT_X / 2, P.OUT_X / 2, '#f3d9b1', '#863', 'lid (tape face)')
for sx in (-1, 1):
    hband(P.BOTTOM_WALL, P.CEIL_Z, sx * P.OUT_X / 2, sx * (P.OUT_X / 2 - P.WALL),
          '#cfe0f5', '#456')
    # X locating ribs + the ledges the board lands on
    hband(P.BOTTOM_WALL, P.PCB_BACK_Z + P.PCB_T + P.RIB_TOP_Z,
          sx * P.RIB_X, sx * (P.RIB_X + P.RIB_T), '#bcd4ee', '#456',
          'locating rib + ledge' if sx < 0 else None)
    hband(P.PCB_BACK_Z - 2.0, P.PCB_BACK_Z,
          sx * P.RIB_X, sx * (P.RIB_X - P.LEDGE_W), '#bcd4ee', '#456')
# button
hband(-P.BUTTON_HEAD_T, 0, -P.BUTTON_HEAD_D / 2, P.BUTTON_HEAD_D / 2, '#6e7480', '#333',
      'button head (dome)')
hband(0, P.BUTTON_INSIDE_T, -P.BUTTON_BODY_D / 2, P.BUTTON_BODY_D / 2, '#b8bcc4', '#555',
      f'button body — MEASURED {P.BUTTON_OVERALL_T} tip to tail')
hband(P.BUTTON_PANEL_T, P.BUTTON_PANEL_T + P.BUTTON_NUT_T,
      -P.BUTTON_NUT_AC / 2, P.BUTTON_NUT_AC / 2, 'none', '#a00', 'M12 nut', hatch='///', z=5)
hband(P.BUTTON_INSIDE_T, P.PCB_BACK_Z,
      -P.BUTTON_BODY_D / 2, P.BUTTON_BODY_D / 2, '#fee', '#a00', 'wire tail')
# board
hband(P.PCB_BACK_Z, P.PCB_BACK_Z + P.PCB_T, -P.PCB_W / 2, P.PCB_W / 2, '#2a6b32', '#063',
      'PCB (underside FLAT)')
hband(P.PCB_BACK_Z + P.PCB_T, P.PCB_BACK_Z + P.SHIELD_Z, P.SHIELD_X0, P.SHIELD_X1,
      '#9ab', '#456', 'RF shield can')
hband(P.PCB_BACK_Z + P.PCB_T, P.PCB_BACK_Z + P.COMP_Z_MAX, P.USB_X0, P.USB_X1,
      '#c9b', '#639', 'USB-C (tallest, 4.46)', z=1)
# the lid rib — the whole point of this panel
hband(P.CEIL_Z - P.LID_RIB_H, P.CEIL_Z, -P.LID_RIB_W / 2, P.LID_RIB_W / 2,
      '#e8a33d', '#863', 'lid hold-down rib', z=6)
a.annotate('', xy=(0, P.CEIL_Z), xytext=(0, P.PCB_BACK_Z + P.SHIELD_Z),
           arrowprops=dict(arrowstyle='<|-|>', color='#d00', lw=1.4))
a.text(1.0, (P.CEIL_Z + P.PCB_BACK_Z + P.SHIELD_Z) / 2,
       f'rib {P.LID_RIB_H:.2f}\n(~0.2 squeeze)', color='#d00', fontsize=6.5,
       ha='left', va='center', weight='bold')
a.annotate('', xy=(-P.OUT_X / 2 - 3, 0), xytext=(-P.OUT_X / 2 - 3, P.HEIGHT),
           arrowprops=dict(arrowstyle='<|-|>', color='#222', lw=1.4))
a.text(-P.OUT_X / 2 - 4, P.HEIGHT / 2, f'{P.HEIGHT:.2f} mm', rotation=90,
       ha='right', va='center', fontsize=9, weight='bold')
a.text(0, -4.4, f'height is the BUTTON: measured {P.BUTTON_OVERALL_T} tip-to-tail − '
                f'{P.BUTTON_HEAD_T} dome + {P.BUTTON_TAIL_T} wire tail = {P.PCB_BACK_Z} '
                f'before the board contributes anything',
       color='#a00', fontsize=6.8, ha='center', weight='bold')
a.set_xlim(-P.OUT_X / 2 - 12, P.OUT_X / 2 + 26); a.set_ylim(-6, P.HEIGHT + 4)
a.set_aspect('equal'); a.axis('off')
a.legend(loc='center right', fontsize=6.0, framealpha=0.92, labelspacing=0.3)

# ---- (3) bottom view --------------------------------------------------------------
a = fig.add_subplot(2, 2, 3)
a.set_title("Bottom — the face you press (button Ø14 head)")
a.add_patch(Rectangle((-P.OUT_X / 2, -P.OUT_Y / 2), P.OUT_X, P.OUT_Y,
                      fc='#eef2f7', ec='#888', lw=1.2))
a.add_patch(Circle((P.BUTTON_CX, P.BUTTON_CY), P.BUTTON_HEAD_D / 2, fc='#c9ced6',
                   ec='#444', lw=1.4))
a.add_patch(Circle((P.BUTTON_CX, P.BUTTON_CY), P.BUTTON_BORE_D / 2, fc='#8f959e',
                   ec='#444', lw=0.8, ls='--'))
a.text(P.BUTTON_CX, P.BUTTON_CY - P.BUTTON_HEAD_D / 2 - 1.2,
       f'FLM12-FJ-6  bore Ø{P.BUTTON_BORE_D}', color='#333', fontsize=7,
       ha='center', va='top')
for sx in (-1, 1):
    for sy in (-1, 1):
        a.add_patch(Circle((sx * P.LID_POST_X, sy * P.LID_POST_Y), P.LID_POST_OD / 2,
                           fc='none', ec='#a0f', lw=1.0))
a.text(0, P.OUT_Y / 2 + 1.0, 'lid screws land on the OTHER face (○ = columns)',
       color='#a0f', fontsize=6.5, ha='center', va='bottom')
a.plot([-P.USB_SLOT_W / 2, P.USB_SLOT_W / 2], [-P.OUT_Y / 2, -P.OUT_Y / 2],
       color='#e07000', lw=4, solid_capstyle='butt')
a.text(0, -P.OUT_Y / 2 - 1.2, f'USB-C notch {P.USB_SLOT_W:.0f} wide + pinch ribs '
                              f'(strain relief = the missing mounting holes)',
       color='#e07000', fontsize=6.4, ha='center', va='top')
a.set_aspect('equal'); a.set_xlim(-24, 24); a.set_ylim(-22, 20); a.axis('off')

# ---- (4) interior plan ------------------------------------------------------------
a = fig.add_subplot(2, 2, 4)
a.set_title("Interior plan — STEP-verified board features and what sits where")
a.add_patch(Rectangle((-P.IN_X / 2, -P.IN_Y / 2), P.IN_X, P.IN_Y,
                      fc='#f7f9fc', ec='#888', lw=1.0))
# the button + nut wrench circle
a.add_patch(RegularPolygon((P.BUTTON_CX, P.BUTTON_CY), 6, radius=P.BUTTON_NUT_AC / 2,
                           orientation=np.pi / 6, fc='none', ec='#a00', lw=1.2, ls='--',
                           zorder=4))
a.add_patch(Circle((P.BUTTON_CX, P.BUTTON_CY), P.BUTTON_BORE_D / 2, fc='#c9ced6',
                   ec='#333', lw=1.4, zorder=4))
a.text(P.BUTTON_CX, P.BUTTON_CY, 'BORE', fontsize=6.0, ha='center', va='center', zorder=6)
# the board and its features
a.add_patch(Rectangle((-P.PCB_W / 2, -P.PCB_L / 2), P.PCB_W, P.PCB_L,
                      fc='none', ec='#159', lw=1.6))
a.add_patch(Rectangle((P.SHIELD_X0, P.SHIELD_Y0), P.SHIELD_X1 - P.SHIELD_X0,
                      P.SHIELD_Y1 - P.SHIELD_Y0, fc='#dde6ef', ec='#456', lw=0.9, zorder=2))
a.add_patch(Rectangle((-P.LID_RIB_W / 2, P.LID_RIB_Y - P.LID_RIB_T / 2), P.LID_RIB_W,
                      P.LID_RIB_T, fc='#e8a33d', ec='#863', lw=1.0, zorder=5))
a.text(0, P.LID_RIB_Y, 'lid rib', fontsize=5.6, ha='center', va='center', zorder=6)
a.add_patch(Rectangle((P.UFL_X0, P.UFL_Y0), P.UFL_X1 - P.UFL_X0, P.UFL_Y1 - P.UFL_Y0,
                      fc='#cfe8d8', ec='#0a8', lw=1.0, zorder=3))
a.add_patch(Rectangle((P.USB_X0, P.USB_Y_EDGE), P.USB_X1 - P.USB_X0,
                      P.USB_Y_INNER - P.USB_Y_EDGE, fc='#e6d9f2', ec='#639', lw=1.0, zorder=3))
a.text(0, (P.USB_Y_EDGE + P.USB_Y_INNER) / 2, 'USB-C', fontsize=5.6, ha='center',
       va='center', zorder=6)
# ribs + ledges + columns
for sx in (-1, 1):
    a.add_patch(Rectangle((min(sx * P.RIB_X, sx * (P.RIB_X + P.RIB_T)), -P.IN_Y / 2),
                          P.RIB_T, P.IN_Y, fc='#bcd4ee', ec='#456', lw=0.7, zorder=1))
    for sy in (-1, 1):
        a.add_patch(Rectangle((min(sx * P.RIB_X, sx * (P.RIB_X - P.LEDGE_W)),
                               sy * P.LEDGE_Y - P.LEDGE_L / 2), P.LEDGE_W, P.LEDGE_L,
                              fc='#8fb6e0', ec='#456', lw=0.7, zorder=3))
        a.add_patch(Circle((sx * P.LID_POST_X, sy * P.LID_POST_Y), P.LID_POST_OD / 2,
                           fc='#efe0ff', ec='#a0f', lw=0.8, zorder=3))
a.annotate('4 ledges — the ENTIRE board mount.\nThe XIAO has no mounting holes, and\n'
           'its underside is flat (STEP-verified).',
           xy=(-P.RIB_X + 0.7, -P.LEDGE_Y), xytext=(-24, -22),
           color='#456', fontsize=5.8, ha='left', va='center',
           arrowprops=dict(arrowstyle='->', color='#456', lw=0.8))
a.annotate('u.FL — antenna drops through the +Y\nnotch and lies UNDER the board,\n'
           '~10 mm off the button body',
           xy=((P.UFL_X0 + P.UFL_X1) / 2, P.UFL_Y1), xytext=(14, 19),
           color='#0a8', fontsize=5.8, ha='left', va='center',
           arrowprops=dict(arrowstyle='->', color='#0a8', lw=0.8))
a.annotate('shield can — flat, rigid, dead.\nThe rib bears HERE, never on a connector.',
           xy=(P.SHIELD_X1, P.SHIELD_Y0 + 1.0), xytext=(12, -13),
           color='#863', fontsize=5.8, ha='left', va='center',
           arrowprops=dict(arrowstyle='->', color='#863', lw=0.8))
a.text(-P.IN_X / 2, P.IN_Y / 2 + 1.4,
       f'PCB {P.PCB_W}×{P.PCB_L}×{P.PCB_T} — from the vendor STEP '
       f'(XIAO-ESP32S3 v2.step). No mounting holes.',
       color='#159', fontsize=6.0, ha='left', va='bottom')
a.set_aspect('equal'); a.set_xlim(-25, 34); a.set_ylim(-25, 24); a.axis('off')

plt.tight_layout(rect=(0, 0, 1, 0.955))
plt.savefig('render_preview.png', dpi=120)
print("wrote render_preview.png")
