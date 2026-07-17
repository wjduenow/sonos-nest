#!/usr/bin/env python3
"""
Render render_preview.png (2x2):
  (1) 3D view of the assembled case, from the real meshes, with a stand-in board + button
  (2) side section through the bore (x = BUTTON_CX) -- THE drawing: it shows the button
      body and the board interleaved in Z rather than stacked, which is the whole design
  (3) bottom view -- the button face, what the user reaches up and presses
  (4) interior plan: the header channel the button lives in, the J4 pinout, and every
      keep-out the layout had to dodge

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
fig.suptitle(f"sonos-button — ESP32-S3-CAM + FLM12-FJ-6, taped under a nightstand   "
             f"({P.OUT_X:.1f} × {P.OUT_Y:.1f} × {P.HEIGHT:.2f} mm)",
             fontsize=14, weight='bold')

C_SHELL, C_LID, C_PCB, C_BTN = '#93b4dd', '#e0b878', '#2a6b32', '#b8bcc4'


def blk(ext, ctr):
    m = trimesh.creation.box(extents=ext); m.apply_translation(ctr); return m


# a stand-in board + button so the preview reads as an assembly
pcb = blk((P.PCB_W, P.PCB_L, P.PCB_T), (0, 0, P.PCB_BACK_Z + P.PCB_T / 2))
btn_body = trimesh.creation.cylinder(radius=P.BUTTON_BODY_D / 2,
                                     height=P.BUTTON_BEHIND_T, sections=64)
btn_body.apply_translation([P.BUTTON_CX, P.BUTTON_CY,
                            P.BUTTON_PANEL_T + P.BUTTON_BEHIND_T / 2])
btn_head = trimesh.creation.cylinder(radius=P.BUTTON_HEAD_D / 2, height=1.5, sections=64)
btn_head.apply_translation([P.BUTTON_CX, P.BUTTON_CY, -0.75])

# ---- (1) 3D ---------------------------------------------------------------------
a = fig.add_subplot(2, 2, 1, projection='3d')
a.set_title("Exploded — assembly order (button face DOWN, tape face UP)")
light = np.array([-0.4, -0.5, 0.75]); light /= np.linalg.norm(light)


def shaded(m, hexc):
    base = np.array(mcolors.to_rgb(hexc))
    b = 0.55 + 0.45 * np.clip(m.face_normals @ light, 0, 1)
    rgba = np.ones((len(m.faces), 4)); rgba[:, :3] = np.clip(base[None, :] * b[:, None], 0, 1)
    return rgba


# exploded along Z -- matplotlib's 3D has no depth sorting, so a closed box just renders
# as a mess; pulling the parts apart is both clearer and honest about the build order.
EXPLODE = ((shell, C_SHELL, 0.0), (btn_body, C_BTN, -20.0), (btn_head, '#6e7480', -20.0),
           (pcb, C_PCB, 12.0), (lid, C_LID, 26.0))
for m, hexc, dz in EXPLODE:
    mm = m.copy(); mm.apply_translation([0, 0, dz])
    a.add_collection3d(Poly3DCollection(mm.triangles, facecolors=shaded(mm, hexc),
                                        edgecolors='none', linewidths=0))
a.set_xlim(-30, 30); a.set_ylim(-28, 28); a.set_zlim(-24, 56)
a.set_box_aspect((60, 56, 80))
a.view_init(elev=14, azim=-66)
a.set_axis_off()
for txt, fy, col in (("4. lid = TAPE FACE → nightstand   (4 × M3×10 flat, flush)", 0.90, '#8a6a2a'),
                     ("3. board drops in   (4 × M3×8 flat)", 0.70, '#063'),
                     ("2. shell", 0.50, '#456'),
                     ("1. button up through the bore, M12 nut inside", 0.10, '#333')):
    a.text2D(0.0, fy, txt, transform=a.transAxes, color=col, fontsize=7, ha='left',
             weight='bold')

# ---- (2) side section through the bore -------------------------------------------
a = fig.add_subplot(2, 2, 2)
a.set_title(f"Section through the bore (x={P.BUTTON_CX:.0f}) — button NESTS beside the pins")
for m, fc in ((shell, '#cfe0f5'), (lid, '#f3d9b1')):
    sec = m.section(plane_origin=[P.BUTTON_CX, 0, 0], plane_normal=[1, 0, 0])
    if sec is None:
        continue
    planar, _ = sec.to_planar()
    for poly in planar.polygons_full:
        xy = np.array(poly.exterior.coords)
        # to_planar puts the section in its own 2D frame; map back to (y, z)
        a.fill(xy[:, 0], xy[:, 1], fc=fc, ec='k', lw=0.7, zorder=2)

# overlay the real stack in true (y, z)
a2 = a.twinx(); a2.set_axis_off()
a.set_aspect('equal'); a.axis('off')
a.set_title(f"Section through the bore (x={P.BUTTON_CX:.0f})", fontsize=11)

# The section frame is fiddly; draw the stack explicitly instead -- it is the point.
a.clear(); a.set_title("The Z stack — button and board INTERLEAVE, they don't stack")


def hband(z0, z1, x0, x1, fc, ec, label=None, hatch=None):
    a.add_patch(Rectangle((x0, z0), x1 - x0, z1 - z0, fc=fc, ec=ec, lw=1.0,
                          zorder=2, label=label, hatch=hatch))


hband(0, P.BOTTOM_WALL, -P.OUT_X / 2, P.OUT_X / 2, '#cfe0f5', '#456', 'shell')
hband(P.CEIL_Z, P.HEIGHT, -P.OUT_X / 2, P.OUT_X / 2, '#f3d9b1', '#863', 'lid (tape face)')
for sx in (-1, 1):
    hband(P.BOTTOM_WALL, P.CEIL_Z, sx * P.OUT_X / 2, sx * (P.OUT_X / 2 - P.WALL),
          '#cfe0f5', '#456')
# button
hband(-1.5, 0, -P.BUTTON_HEAD_D / 2, P.BUTTON_HEAD_D / 2, '#6e7480', '#333', 'button head')
hband(P.BUTTON_PANEL_T, P.BUTTON_PANEL_T + P.BUTTON_BEHIND_T,
      -P.BUTTON_BODY_D / 2, P.BUTTON_BODY_D / 2, '#b8bcc4', '#555', 'button body')
hband(P.BUTTON_PANEL_T, P.BUTTON_PANEL_T + P.BUTTON_NUT_T,
      -P.BUTTON_NUT_AC / 2, P.BUTTON_NUT_AC / 2, 'none', '#a00', 'M12 nut', hatch='///')
hband(P.BUTTON_PANEL_T + P.BUTTON_BEHIND_T, P.PCB_BACK_Z,
      -P.BUTTON_BODY_D / 2, P.BUTTON_BODY_D / 2, '#fee', '#a00', 'wire exit (TAIL)')
# board
hband(P.PCB_BACK_Z, P.PCB_BACK_Z + P.PCB_T, -P.PCB_W / 2, P.PCB_W / 2, '#2a6b32', '#063',
      'PCB')
hband(P.PCB_BACK_Z + P.PCB_T, P.PCB_BACK_Z + P.COMP_Z_MAX, -8, 8, '#9ab', '#456',
      'top-side parts')
# the headers — the two rows the button sits between
for sx in (-1, 1):
    hband(P.PCB_BACK_Z - P.BOARD_BELOW_T, P.PCB_BACK_Z,
          sx * P.HDR_X - P.HDR_BODY / 2, sx * P.HDR_X + P.HDR_BODY / 2,
          '#333', '#000', 'header pins (J4/J5)' if sx < 0 else None)
a.annotate('', xy=(-P.CHANNEL_W / 2, 6.5), xytext=(P.CHANNEL_W / 2, 6.5),
           arrowprops=dict(arrowstyle='<|-|>', color='#d00', lw=1.6))
a.text(0, -3.2, f'channel {P.CHANNEL_W:.2f} — bore keep-out '
                f'Ø{P.BUTTON_BORE_D + 2*P.BUTTON_CLR:.1f}',
       color='#d00', fontsize=7.5, ha='center', weight='bold')
a.annotate('', xy=(-P.OUT_X / 2 - 3, 0), xytext=(-P.OUT_X / 2 - 3, P.HEIGHT),
           arrowprops=dict(arrowstyle='<|-|>', color='#222', lw=1.4))
a.text(-P.OUT_X / 2 - 4, P.HEIGHT / 2, f'{P.HEIGHT:.2f} mm', rotation=90,
       ha='right', va='center', fontsize=9, weight='bold')
a.set_xlim(-P.OUT_X / 2 - 12, P.OUT_X / 2 + 30); a.set_ylim(-5, P.HEIGHT + 5)
a.set_aspect('equal'); a.axis('off')
a.legend(loc='center right', fontsize=6.2, framealpha=0.92, labelspacing=0.35)

# ---- (3) bottom view --------------------------------------------------------------
a = fig.add_subplot(2, 2, 3)
a.set_title("Bottom — the face you press (button Ø14 head)")
a.add_patch(Rectangle((-P.OUT_X / 2, -P.OUT_Y / 2), P.OUT_X, P.OUT_Y,
                      fc='#eef2f7', ec='#888', lw=1.2))
a.add_patch(Circle((P.BUTTON_CX, P.BUTTON_CY), P.BUTTON_HEAD_D / 2, fc='#c9ced6',
                   ec='#444', lw=1.4))
a.add_patch(Circle((P.BUTTON_CX, P.BUTTON_CY), P.BUTTON_BORE_D / 2, fc='#8f959e',
                   ec='#444', lw=0.8, ls='--'))
a.text(P.BUTTON_CX, P.BUTTON_CY - P.BUTTON_HEAD_D / 2 - 1.5,
       f'FLM12-FJ-6  bore Ø{P.BUTTON_BORE_D}', color='#333', fontsize=7,
       ha='center', va='top')
for sx in (-1, 1):
    for sy in (-1, 1):
        a.add_patch(Circle((sx * P.LID_POST_X, sy * P.LID_POST_Y), P.LID_POST_OD / 2,
                           fc='none', ec='#a0f', lw=1.0))
a.text(0, -P.OUT_Y / 2 + 1.5, 'lid screws land on the OTHER face (○ = columns)',
       color='#a0f', fontsize=6.5, ha='center', va='bottom')
a.plot([-P.USB_SLOT_W / 2, P.USB_SLOT_W / 2], [-P.OUT_Y / 2, -P.OUT_Y / 2],
       color='#e07000', lw=4, solid_capstyle='butt')
a.text(0, -P.OUT_Y / 2 - 1.5, f'USB-C notch {P.USB_SLOT_W:.0f} wide (permanent power)',
       color='#e07000', fontsize=7, ha='center', va='top')
a.set_aspect('equal'); a.set_xlim(-30, 30); a.set_ylim(-28, 26); a.axis('off')

# ---- (4) interior plan ------------------------------------------------------------
a = fig.add_subplot(2, 2, 4)
a.set_title("Interior plan — the channel, and everything the bore had to dodge")
a.add_patch(Rectangle((-P.IN_X / 2, -P.IN_Y / 2), P.IN_X, P.IN_Y,
                      fc='#f7f9fc', ec='#888', lw=1.0))
a.add_patch(Rectangle((-P.PCB_W / 2, -P.PCB_L / 2), P.PCB_W, P.PCB_L,
                      fc='none', ec='#159', lw=1.4, ls='-'))
a.text(-31, 27, f'PCB {P.PCB_W}×{P.PCB_L}, holes Ø{P.HOLE_D} on {P.HOLE_DX}×{P.HOLE_DY}\n'
                f'headers at x=±{P.HDR_X}, {P.HDR_PITCH} pitch — all from the vendor STEP',
       color='#159', fontsize=6, ha='left', va='top')

# the two header rows -- the measurement the whole design rests on
J4 = ['5V', 'GND', 'IO1', 'IO48', 'NC', 'IO47', 'IO14', 'BOOT']
J5 = ['3V3', 'GND', 'IO46', 'IO41', 'IO42', 'IO43', 'IO44', 'EN']
USED = {'5V', 'GND', 'IO47', 'IO14'}
for row, (sx, names, tag) in enumerate(((-1, J4, 'J4'), (1, J5, 'J5'))):
    for i, nm in enumerate(names):
        py = P.HDR_Y0 + i * P.HDR_PITCH
        used = sx < 0 and nm in USED
        a.add_patch(Circle((sx * P.HDR_X, py), 0.9,
                           fc='#d00' if used else '#333', ec='k', lw=0.4, zorder=5))
        a.text(sx * (P.HDR_X + 1.6), py, nm, fontsize=5.6, va='center',
               ha='right' if sx < 0 else 'left',
               color='#d00' if used else '#666', weight='bold' if used else 'normal')
    a.add_patch(Rectangle((sx * P.HDR_X - P.HDR_BODY / 2, P.HDR_Y0 - 1.27),
                          P.HDR_BODY, 7 * P.HDR_PITCH + 2.54,
                          fc='#444', ec='k', lw=0.6, alpha=0.35, zorder=1))
    a.text(sx * P.HDR_X, 9.2, tag, fontsize=9, ha='center', va='bottom',
           color='#000', weight='bold')

# THE dimension: witness lines off the header bodies, dimension carried clear of the board
DIM_Y = 22.5
for sx in (-1, 1):
    a.plot([sx * P.CHANNEL_W / 2] * 2, [7.9, DIM_Y + 0.6], color='#d00', lw=0.6, ls=':')
a.annotate('', xy=(-P.CHANNEL_W / 2, DIM_Y), xytext=(P.CHANNEL_W / 2, DIM_Y),
           arrowprops=dict(arrowstyle='<|-|>', color='#d00', lw=1.5))
a.text(0, DIM_Y + 0.7, f'channel {P.CHANNEL_W:.2f} mm — bore keep-out '
                       f'Ø{P.BUTTON_BORE_D + 2*P.BUTTON_CLR:.1f}, so it fits',
       color='#d00', fontsize=6.8, ha='center', va='bottom', weight='bold')

# the button
a.add_patch(RegularPolygon((P.BUTTON_CX, P.BUTTON_CY), 6, radius=P.BUTTON_NUT_AC / 2,
                           orientation=np.pi / 6, fc='none', ec='#a00', lw=1.2, ls='--',
                           zorder=4))
a.add_patch(Circle((P.BUTTON_CX, P.BUTTON_CY), P.BUTTON_BORE_D / 2, fc='#c9ced6',
                   ec='#333', lw=1.4, zorder=4))
a.text(P.BUTTON_CX, P.BUTTON_CY, 'BORE', fontsize=6.5, ha='center', va='center', zorder=6)
a.annotate(f'16 A/F nut (Ø{P.BUTTON_NUT_AC:.1f} A/C)',
           xy=(P.BUTTON_CX + P.BUTTON_NUT_AC / 2 * 0.87, P.BUTTON_CY - 4.6),
           xytext=(24, -2.0), color='#a00', fontsize=6, ha='left', va='center',
           arrowprops=dict(arrowstyle='->', color='#a00', lw=0.8))

# the obstructions it dodges
for nm, (x0, x1, y0, y1, d) in P.BOTTOM_PARTS.items():
    a.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc='#ffd9d9', ec='#c33',
                          lw=1.0, hatch='xxx', zorder=3))
    a.text((x0 + x1) / 2, (y0 + y1) / 2, nm.split()[0], color='#900', fontsize=6,
           ha='center', va='center', zorder=6)
a.text(0, -P.IN_Y / 2 - 1.0,
       'xxx = soldered BOTTOM-side JSTs — unused, but real: J1 is why the bore sits at y=+4',
       color='#900', fontsize=6, ha='center', va='top')

for sx in (-1, 1):
    for sy in (-1, 1):
        a.add_patch(Circle((sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2), P.BOSS_OD / 2,
                           fc='#cfe0f5', ec='#456', lw=0.8, zorder=3))
        a.add_patch(Circle((sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2), P.HOLE_D / 2,
                           fc='#fff', ec='#456', lw=0.5, zorder=4))
        a.add_patch(Circle((sx * P.LID_POST_X, sy * P.LID_POST_Y), P.LID_POST_OD / 2,
                           fc='#efe0ff', ec='#a0f', lw=0.8, zorder=3))
for px, py in P.WIRE_POSTS:
    a.add_patch(Circle((px, py), P.WIRE_POST_D / 2, fc='#dfe', ec='#293', lw=0.8, zorder=3))
a.annotate('wire posts — the 150 mm\npigtail coils around the\nbutton, under the pin tips',
           xy=(P.WIRE_POSTS[0][0] + 2.0, P.WIRE_POSTS[0][1] - 3.5), xytext=(24, -12),
           color='#293', fontsize=5.8, ha='left', va='center',
           arrowprops=dict(arrowstyle='->', color='#293', lw=0.8))
a.plot([-P.PCB_W / 2, P.PCB_W / 2], [P.ANTENNA_KEEPOUT_Y] * 2, color='#0a8', lw=1.0, ls=':')
a.annotate('⚠ PCB antenna end — etched, so NOT in the STEP;\n'
           'inferred from the component-free strip above ┈┈',
           xy=(P.PCB_W / 2 - 2, P.ANTENNA_KEEPOUT_Y), xytext=(24, 12.5),
           color='#0a8', fontsize=5.8, ha='left', va='center',
           arrowprops=dict(arrowstyle='->', color='#0a8', lw=0.8))
a.set_aspect('equal'); a.set_xlim(-32, 44); a.set_ylim(-27, 30); a.axis('off')

plt.tight_layout(rect=(0, 0, 1, 0.955))
plt.savefig('render_preview.png', dpi=120)
print("wrote render_preview.png")
