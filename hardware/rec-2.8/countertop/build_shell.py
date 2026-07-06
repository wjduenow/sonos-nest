#!/usr/bin/env python3
"""
Build shell.stl -- the wedge body that holds the ES3C28P board reclined 20deg on a
nightstand.  The board drops into a pocket on the reclined front face (screen out),
sits on 4 self-tapping bosses at its corner holes, and is framed by bezel.stl.

Features:
  * reclined front face with a board-shaped pocket + central component cavity
  * 4 mounting bosses (pilot holes) at the 42 x 78 mm hole pattern
  * 4 bezel screw posts in the top/bottom face margins
  * USB-C slot through the -X short-edge wall
  * RESET pin hole + microSD access window bored through the back wall
  * flat base for a stable 20deg stance

    python3 build_shell.py   # -> shell.stl

Geometry / board numbers live in stand_params.py.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, box, extrude_polygon
from trimesh.boolean import union, difference
from trimesh.transformations import rotation_matrix, translation_matrix
from shapely.geometry import Polygon
from shapely.geometry import box as sbox
import stand_params as P

def rrect(hx, hy, r):
    return sbox(-(hx - r), -(hy - r), (hx - r), (hy - r)).buffer(r, resolution=16)

T   = np.radians(P.TILT_DEG)
sT, cT = np.sin(T), np.cos(T)
SEG = P.SEG

# ---- local board frame -> world -------------------------------------------------
# up-incline world direction = (0, sinT, cosT); screen normal (out) = (0,-cosT, sinT)
# realised as a rotation about world X by (90 - tilt) degrees.
_R = rotation_matrix(np.radians(90 - P.TILT_DEG), [1, 0, 0])
_s_center = P.MB + P.PCB_H / 2.0                       # board centre up the face
_center = [0.0, P.FRONT_LIP + _s_center * sT, _s_center * cT]
_M = translation_matrix(_center) @ _R                 # world = center + R @ local

def to_world(mesh):
    m = mesh.copy(); m.apply_transform(_M); return m

def cyl(r, h, z0=0.0, x=0.0, y=0.0):
    """cylinder along local Z, base at z0, then placed in local frame."""
    m = cylinder(radius=r, height=h, sections=SEG)
    m.apply_translation([x, y, z0 + h / 2.0]); return m

def lbox(ext, ctr):
    m = box(extents=ext); m.apply_translation(ctr); return m

# ---- outer wedge body -----------------------------------------------------------
def wedge_body():
    """Side profile in (depth, height) extruded across the board width X."""
    top = P.FACE_LEN                                  # face length up the incline
    face_top = (top * sT, top * cT)                   # (y,z) of the face top
    prof = Polygon([(0.0, 0.0),                       # face bottom, at ground/front
                    face_top,                         # face top (leans back)
                    (P.BACK_Y, face_top[1]),          # back-top
                    (P.BACK_Y, 0.0)])                 # back-bottom
    body = extrude_polygon(prof, height=P.W_OUT)      # extruded along +Z (0..W_OUT)
    # remap extruded axes (depth,height,width) -> world (X=width, Y=depth, Z=height)
    Pm = np.array([[0, 0, 1, -P.W_OUT / 2.0],
                   [1, 0, 0, 0],
                   [0, 1, 0, 0],
                   [0, 0, 0, 1]], dtype=float)
    body.apply_transform(Pm)
    return body

# ---- board pocket + component cavity (local frame cutters) ----------------------
def pocket_cutter():
    floor = -(P.PCB_T + P.COMP_H + P.BACK_CLR)        # z of the pocket floor
    h = 2.0 - floor                                   # +2 in front to cut the face
    return lbox((P.PCB_W + 2 * P.FIT, P.PCB_H + 2 * P.FIT, h),
                (0, 0, (2.0 + floor) / 2.0))

def bosses():
    floor = -(P.PCB_T + P.COMP_H + P.BACK_CLR)
    top = -P.PCB_T                                    # boss top meets the PCB back
    out = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            out.append(cyl(P.BOSS_OD / 2, top - floor, floor,
                           sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2))
    return out

def boss_pilots():
    top = -P.PCB_T
    out = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            out.append(cyl(P.BOSS_PILOT / 2, 8.0, top - 8.0 + 0.1,
                           sx * P.HOLE_DX / 2, sy * P.HOLE_DY / 2))
    return out

def rim():
    """Raised frame around the pocket (local z 0..RIM_H) that the bezel caps FLUSH.
    Outer == shell face outer, inner == pocket opening -> continuous, no gap."""
    outer = rrect(P.BEZEL_OUT_X, P.BEZEL_OUT_Y, P.BEZEL_R)
    inner = rrect(P.PCB_W / 2 + P.FIT, P.PCB_H / 2 + P.FIT, 2.0)
    frame = outer.difference(inner)
    return extrude_polygon(frame, P.RIM_H)

def rim_pilots():
    # bezel screws land in the top/bottom rim band
    out = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            out.append(cyl(P.POST_PILOT / 2, 9.0, P.RIM_H - 8.5,
                           sx * P.POST_X, sy * P.POST_Y))
    return out

def usb_cut():
    """BLIND internal clearance for the right-angle male plug on the board's -X USB-C.
    The -X exterior stays CLOSED (USB_SKIN skin) -- the legacy side port is removed;
    power now enters via the rear panel jack.  Local frame (local x == world x)."""
    x_out = -P.W_OUT / 2 + P.USB_SKIN          # inner edge of the retained outer skin
    x_in = -P.PCB_W / 2 + 3.0                   # a few mm under the board edge
    return lbox((x_in - x_out, P.USB_SLOT_Y, P.USB_SLOT_Z),
                ((x_in + x_out) / 2, P.USB_Y, -P.USB_SLOT_Z / 2 + 1.5))

def _yprism(w, h, depth, xc, y0, zc, corner=1.0):
    """A (rounded) rectangular prism, w (x) x h (z), extruded along +Y by `depth`
    from y0.  Used for the back-wall panel-mount features (back wall is a flat y-plane)."""
    poly = rrect(w / 2, h / 2, corner)                 # 2D in XY -> (x, z_of_wall)
    m = extrude_polygon(poly, depth)                   # extruded along +Z (the depth = Y)
    Pm = np.array([[1, 0, 0, xc], [0, 0, 1, y0], [0, 1, 0, zc], [0, 0, 0, 1]], float)
    m.apply_transform(Pm)                              # -> (x, y=y0..y0+depth, z)
    return m

def panel_mount():
    """Rear panel-mount USB-C jack on the flat vertical back wall (y = BACK_Y):
    a flush flange recess, a body/ribbon pocket behind it (narrow in x so the two
    17 mm screws keep SOLID columns; dropping down + forward to the board cavity), and
    the two screw pilots."""
    x, z = P.PANEL_X, P.PANEL_Z
    cuts = []
    # flush racetrack recess for the female flange, opening at the back face
    cuts.append(_yprism(P.PANEL_FLANGE_W, P.PANEL_FLANGE_H, P.PANEL_RECESS_D + 0.2,
                        x, P.BACK_Y - P.PANEL_RECESS_D, z, corner=P.PANEL_FLANGE_H / 2))
    # connector-body + ribbon pocket: behind the recess floor -> forward to the board
    # cavity.  Width 12 keeps x=±8.5 screw columns solid; tall + dropped for the ribbon.
    pz1 = z + P.PANEL_ROUTE_UP
    pz0 = pz1 - P.PANEL_ROUTE_H
    depth = P.BACK_Y - P.PANEL_RECESS_D - P.PANEL_ROUTE_Y0
    cuts.append(_yprism(P.PANEL_ROUTE_W, pz1 - pz0, depth,
                        x, P.PANEL_ROUTE_Y0, (pz0 + pz1) / 2, corner=2.0))
    # two screw pilots into the solid columns beside the pocket (M2.5 self-tap)
    for sx in (-1, 1):
        p = cylinder(radius=P.PANEL_SCREW_PILOT / 2, height=9.0, sections=SEG)
        p.apply_transform(rotation_matrix(np.radians(90), [1, 0, 0]))   # axis -> Y
        p.apply_translation([x + sx * P.PANEL_SCREW_DX / 2, P.BACK_Y - 4.5 + 0.1, z])
        cuts.append(p)
    return cuts

def _spk_y():
    """(y0, y1, yc): pocket y-extent and the speaker/grille centre. The speaker sits at
    the FRONT of the pocket; a rear zone (SPK_CAP_ZONE) is left for the snap cap."""
    y1 = P.BACK_Y + 1.0                                       # opens through the back wall
    y0 = P.BACK_Y - (P.SPK_L + P.SPK_FIT) - P.SPK_CAP_ZONE
    yc = y0 + (P.SPK_L + P.SPK_FIT) / 2.0                     # speaker centre (front)
    return y0, y1, yc

def speaker_pocket():
    y0, y1, _ = _spk_y()
    z0, z1 = P.GRILLE_T, P.GRILLE_T + P.SPK_T + P.SPK_FIT
    return box(extents=(P.SPK_W + 2 * P.SPK_FIT, y1 - y0, z1 - z0),
               transform=translation_matrix([P.SPK_CX, (y0 + y1) / 2, (z0 + z1) / 2]))

def speaker_grille():
    _, _, yc = _spk_y()
    holes, nx = [], int(P.SPK_W // P.GRILLE_PITCH)
    ny = int(P.SPK_L // P.GRILLE_PITCH)
    for i in range(-nx // 2, nx // 2 + 1):
        for j in range(-ny // 2, ny // 2 + 1):
            h = cylinder(radius=P.GRILLE_HOLE / 2, height=P.GRILLE_T + 1.0, sections=32)
            h.apply_translation([P.SPK_CX + i * P.GRILLE_PITCH,
                                 yc + j * P.GRILLE_PITCH, P.GRILLE_T / 2])
            holes.append(h)
    return holes

def speaker_wire():
    _, _, yc = _spk_y()
    p0 = [P.SPK_CX, yc, P.GRILLE_T + P.SPK_T]              # pocket ceiling
    p1 = (_M @ np.array([0.0, 0.0, -6.0, 1.0]))[:3]        # inside the board cavity
    return cylinder(radius=P.SPK_WIRE_D / 2, segment=(p0, p1), sections=SEG)

def speaker_cap_catches():
    """Two recesses in the pocket side walls (rear zone) that the cap's hooks snap into."""
    port_hx = P.SPK_W / 2 + P.SPK_FIT
    zc = P.GRILLE_T + (P.SPK_T + P.SPK_FIT) / 2
    cy0, cy1 = P.BACK_Y - 4.0, P.BACK_Y - 1.0              # within the rear cap zone
    out = []
    for sx in (-1, 1):
        out.append(box(extents=(P.CAP_CATCH_DEPTH, cy1 - cy0, P.CAP_ARM_Z + 1.0),
                       transform=translation_matrix(
                           [sx * (port_hx + P.CAP_CATCH_DEPTH / 2), (cy0 + cy1) / 2, zc])))
    return out

def feet():
    out = []
    ov = 1.0                                               # overlap up into the base so it fuses
    for x in (-33.0, 33.0):
        for y in (6.0, P.BACK_Y - 5.0):
            out.append(box(extents=(P.FOOT, P.FOOT, P.FOOT_H + ov),
                           transform=translation_matrix([x, y, (ov - P.FOOT_H) / 2])))
    return out

# ---- back-face access -----------------------------------------------------------
# Bored along the board's LOCAL -Z (straight back through the body, perpendicular to
# the PCB) so the channel exits the rear of the wedge and can NEVER reach the front
# face.  Starts just behind the PCB back plane (z=-1) and runs well past the body.
DEPTH = 44.0

def reset_pin():
    return cyl(P.RESET_PIN_D / 2, DEPTH, -DEPTH - 1.0, P.RESET_X, P.RESET_Y)

def sd_window():
    return lbox((P.SD_WIN_X, P.SD_WIN_Z, DEPTH),
                (P.SD_X, P.SD_Y, -DEPTH / 2 - 1.0))

# ---- assemble -------------------------------------------------------------------
def build_shell():
    body = wedge_body()

    # pocket + cavity (local cutters, sent to world)
    body = difference([body, to_world(pocket_cutter())], engine='manifold')
    body = union([body] + [to_world(b) for b in bosses()], engine='manifold')
    body = union([body, to_world(rim())], engine='manifold')      # flush bezel landing

    cuts = [to_world(usb_cut())]
    cuts += [to_world(p) for p in boss_pilots()]
    cuts += [to_world(p) for p in rim_pilots()]
    cuts.append(to_world(reset_pin()))
    cuts.append(to_world(sd_window()))
    cuts += panel_mount()                                          # rear USB-C jack (world-frame)
    cuts.append(speaker_pocket())
    cuts += speaker_grille()
    cuts.append(speaker_wire())
    cuts += speaker_cap_catches()
    body = difference([body] + cuts, engine='manifold')
    body = union([body] + feet(), engine='manifold')
    # drop zero-volume degenerate slivers the boolean engine leaves at coincident faces
    solids = [p for p in body.split(only_watertight=False) if p.volume > 1.0]
    return solids[0] if len(solids) == 1 else trimesh.util.concatenate(solids)

if __name__ == "__main__":
    m = build_shell(); m.export("shell.stl")
    print(f"shell.stl  watertight={m.is_watertight}  tris={len(m.faces)}  "
          f"bbox(WxDxH)={np.round(m.extents,1)}  mm")
