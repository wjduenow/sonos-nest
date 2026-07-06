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
    # slot through the -X short-edge wall (local), spanning the PCB thickness zone
    x_out = -(P.PCB_W / 2) - 8.0
    return lbox((16.0, P.USB_SLOT_Y, P.USB_SLOT_Z),
                (x_out + 8.0 - 1.0, P.USB_Y, -P.PCB_T / 2 - 0.5))

def cable_channel():
    """Route the cable DOWN the -X face then BACK under the base to the rear.
    The -X side wall is thin, so the routing lives in the SOLID base: an open-bottom
    groove that runs from a front-corner entry notch straight back to a rear exit.
    Cable presses in (no threading); best with a right-angle USB-C plug.
    World frame, since the -X side and the base are world-aligned planes."""
    xw = -P.W_OUT / 2                              # -X exterior face plane
    cx = xw + P.CABLE_W / 2 + 1.5                  # channel just inboard of the -X face
    cuts = []
    # underside groove (open at bottom z<0 and at the rear y>BACK_Y)
    y0, y1 = 2.0, P.BACK_Y + 1.0
    cuts.append(box(extents=(P.CABLE_W, y1 - y0, 2 * P.CABLE_D),
                    transform=translation_matrix([cx, (y0 + y1) / 2, 0.0])))
    # front-corner entry notch: opens the -X-bottom-front so the cable turns in
    cuts.append(box(extents=(2 * (P.CABLE_W), P.CABLE_W, 2 * P.CABLE_D),
                    transform=translation_matrix([xw + P.CABLE_W, 4.0, 0.0])))
    return cuts

def cable_clips():
    """Snap-in nubs bulging from both channel walls near the opening; a ~4 mm cable
    presses past them and is retained. Added back AFTER the channel is cut."""
    xw = -P.W_OUT / 2
    cx = xw + P.CABLE_W / 2 + 1.5              # channel centreline (matches cable_channel)
    half = P.CABLE_W / 2
    out = []
    for y in P.CLIP_YS:
        for side in (-1, 1):
            xc = cx + side * (half - P.CLIP_NUB / 2)   # nub against the wall, protruding in
            out.append(box(extents=(P.CLIP_NUB, P.CLIP_LEN, P.CLIP_H),
                           transform=translation_matrix([xc, y, P.CLIP_H / 2])))
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
    cuts += cable_channel()                                        # already world-frame
    body = difference([body] + cuts, engine='manifold')
    body = union([body] + cable_clips(), engine='manifold')        # snap-in retention
    return body

if __name__ == "__main__":
    m = build_shell(); m.export("shell.stl")
    print(f"shell.stl  watertight={m.is_watertight}  tris={len(m.faces)}  "
          f"bbox(WxDxH)={np.round(m.extents,1)}  mm")
