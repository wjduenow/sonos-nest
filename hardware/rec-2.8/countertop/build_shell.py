#!/usr/bin/env python3
"""
Build shell.stl -- the wedge body that holds the ES3C28P board reclined 20deg on a
nightstand.  The board drops into a pocket on the reclined front face (screen out),
sits on 4 self-tapping bosses at its corner holes, and is framed by bezel.stl.

Features:
  * reclined front face with a board-shaped pocket + central component cavity
  * 4 mounting bosses (pilot holes) at the 42 x 78 mm hole pattern
  * 4 bezel screw pilots in the top/bottom face margins (no raised rim -- the bezel is
    GLASS_PROUD thick and lands flat on the face, ending level with the glass)
  * OPEN USB-C side port through the -X short-edge wall (cable plugs straight in)
  * RESET pin hole bored through the back wall  (no microSD access -- see below)
  * rear-loaded speaker slot (SPK_SLOT_H high) firing down through a grille
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

def bezel_pilots():
    """Bezel screw pilots, bored into the flat face in the top/bottom margins.
    (There is no raised rim any more -- the bezel lands straight on the face.)"""
    out = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            out.append(cyl(P.POST_PILOT / 2, P.POST_DEPTH + 0.5, -P.POST_DEPTH,
                           sx * P.POST_X, sy * P.POST_Y))
    return out

def usb_cut():
    """OPEN side port: a slot through the -X wall so a USB-C cable plugs straight into
    the board's own USB-C.  With USB_SKIN = 0 the cutter is pushed past the outer face
    so the boolean punches clean through (no coincident-face sliver).
    Local frame (local x == world x)."""
    x_out = -P.W_OUT / 2 + P.USB_SKIN          # inner edge of any retained outer skin
    if P.USB_SKIN <= 0.0:
        x_out -= 1.0                            # overshoot the -X face -> real opening
    x_in = -P.PCB_W / 2 + 3.0                   # a few mm under the board edge
    return lbox((x_in - x_out, P.USB_SLOT_Y, P.USB_SLOT_Z),
                ((x_in + x_out) / 2, P.USB_Y, -P.USB_SLOT_Z / 2 + 1.5))

def _spk_y():
    """(y0, y1, yc): pocket y-extent and the speaker/grille centre. The speaker sits at
    the FRONT of the pocket; a rear zone (SPK_CAP_ZONE) is left for the snap cap."""
    y1 = P.BACK_Y + 1.0                                       # opens through the back wall
    y0 = P.BACK_Y - (P.SPK_L + P.SPK_FIT) - P.SPK_CAP_ZONE
    yc = y0 + (P.SPK_L + P.SPK_FIT) / 2.0                     # speaker centre (front)
    return y0, y1, yc

def speaker_pocket():
    """The slot the speaker box slides into: SPK_SLOT_W x SPK_SLOT_H, oversize in X and Z
    so the speaker lead can run beside and above the box."""
    y0, y1, _ = _spk_y()
    z0, z1 = P.GRILLE_T, P.GRILLE_T + P.SPK_SLOT_H
    return box(extents=(P.SPK_SLOT_W, y1 - y0, z1 - z0),
               transform=translation_matrix([P.SPK_CX, (y0 + y1) / 2, (z0 + z1) / 2]))

def _grille_offsets(extent, margin=1.0):
    """Hole-centre offsets, symmetric about 0, that keep the full hole inside `extent`.
    (A plain range(-n//2, n//2+1) floors negatively for odd n -> off-centre grid whose
    first row can fall outside the pocket and drill a blind hole into the solid base.)"""
    span = extent - P.GRILLE_HOLE - 2.0 * margin      # centre-to-centre span available
    if span < 0:
        return [0.0]
    n = int(span // P.GRILLE_PITCH)                   # number of gaps -> n+1 holes
    return [(k - n / 2.0) * P.GRILLE_PITCH for k in range(n + 1)]

def speaker_grille():
    _, _, yc = _spk_y()
    holes = []
    for dx in _grille_offsets(P.SPK_W):
        for dy in _grille_offsets(P.SPK_L):
            h = cylinder(radius=P.GRILLE_HOLE / 2, height=P.GRILLE_T + 1.0, sections=32)
            h.apply_translation([P.SPK_CX + dx, yc + dy, P.GRILLE_T / 2])
            holes.append(h)
    return holes

def speaker_wire():
    """Speaker-lead channel: fans from the pocket ceiling up into the board cavity, with the
    cavity mouth ELONGATED toward the board's -Y (down-incline) edge so the short lead can
    reach the SPEAKER header there.  Built as a union of cylinders from the fixed ceiling
    point to a row of cavity exits sliding from board-local (0,0) to (0,-SPK_WIRE_SLOT)."""
    _, _, yc = _spk_y()
    p0 = [P.SPK_CX, yc, P.GRILLE_T + P.SPK_SLOT_H]         # pocket ceiling (fixed end)
    n = 12
    cuts = []
    for i in range(n + 1):
        yoff = -(i / n) * P.SPK_WIRE_SLOT                  # cavity exit slides toward -Y
        p1 = (_M @ np.array([0.0, yoff, -6.0, 1.0]))[:3]   # inside the board cavity
        cuts.append(cylinder(radius=P.SPK_WIRE_D / 2, segment=(p0, p1), sections=SEG))
    return union(cuts, engine='manifold')

def speaker_cap_catches():
    """Two recesses in the pocket side walls that the cap's hooks snap into.  DERIVED from
    the cap's hook geometry so the retaining shelf lands exactly on the hook's retaining
    corner (they used to be computed independently and missed -> ~0 grip).

    The cap seats when its plate hits the back wall, fixing the hook in world y.  Local
    insertion depth z maps to world y = (BACK_Y + CAP_T) - z.  The retaining corner is at
    z_land; the shelf (solid wall on the +y / pull-out side) starts just short of it by
    CAP_PRELOAD so the seated arm is biased outward with no slop."""
    arm_len = P.SPK_CAP_ZONE - 0.5
    z_tip = P.CAP_T + arm_len
    z_land = z_tip - P.CAP_LEAD_IN
    y_land = (P.BACK_Y + P.CAP_T) - z_land                 # retaining corner, world y
    y_tip = (P.BACK_Y + P.CAP_T) - z_tip                   # deep end of the lead-in
    cy1 = y_land - P.CAP_PRELOAD                            # shelf face (solid at y > cy1)
    cy0 = y_tip - 0.5                                       # recess open back to here
    port_hx = P.SPK_SLOT_W / 2
    zc = P.GRILLE_T + P.SPK_SLOT_H / 2
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

# No microSD access feature: the socket's card mouth is flush with the board's -Y
# (bottom) edge, so the card enters up-incline from below the PCB -- unreachable from
# the rear and blocked by the bottom lip.  The card is swapped with the board out of
# the case; the back is left solid.

# ---- assemble -------------------------------------------------------------------
def build_shell():
    body = wedge_body()

    # pocket + cavity (local cutters, sent to world)
    body = difference([body, to_world(pocket_cutter())], engine='manifold')
    body = union([body] + [to_world(b) for b in bosses()], engine='manifold')
    # no rim: the bezel is GLASS_PROUD thick and lands directly on the flat face

    cuts = [to_world(usb_cut())]
    cuts += [to_world(p) for p in boss_pilots()]
    cuts += [to_world(p) for p in bezel_pilots()]
    cuts.append(to_world(reset_pin()))
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
