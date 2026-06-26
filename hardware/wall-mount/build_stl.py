#!/usr/bin/env python3
"""
Build STLs for the Sonos Knob wall mount (twist-lock bayonet).
Mirrors the parameters in sonos_knob_wall_mount.scad.

Self-contained CSG via trimesh + manifold3d (no OpenSCAD / no display needed).
    pip install --user trimesh manifold3d numpy shapely
    python3 build_stl.py            # -> wall_plate.stl, cradle.stl

Edit the PARAMETERS block after you measure the unit, then re-run.
"""
import numpy as np
import trimesh
from trimesh.creation import cylinder, box, extrude_polygon
from trimesh.boolean import union, difference, intersection
from shapely.geometry import Polygon

# ============================ PARAMETERS ============================
# --- MEASURE THESE on your actual unit (Elecrow only confirms 79x79x30) ---
device_dia    = 79.0     # outer diameter across the back of the body
device_depth  = 30.0     # front face -> back face

# USB-C port
usb_angle_deg = 270.0    # angular position (0 = +X, CCW)
usb_from_back = 8.0      # centre height above the BACK face
usb_w         = 13.0     # opening width  (around circumference)
usb_h         = 8.0      # opening height (along axis)

# --- fit / print tunables ---
clearance   = 0.6        # radial gap around device
wall        = 3.0        # cradle wall thickness
lip_thk     = 2.5        # front retaining lip thickness (axial)
lip_overlap = 3.0        # how far the lip reaches in over the rim (radial)
SEG         = 220        # circle smoothness

# --- bayonet twist-lock ---
ring_h       = 14.0
lug_count    = 3
lug_z        = 9.0
lug_h        = 4.0
lug_w_deg    = 12.0
lug_protrude = 2.0
slot_w       = lug_h + 0.8
twist_deg    = 25.0
ring_fit     = 0.4

# --- wall plate ---
disc_thk    = 4.0
screw_count = 2
screw_bc    = 60.0
screw_d     = 4.4
screw_head  = 8.6
cable_hole  = 11.0
side_notch  = True

# --- derived ---
cradle_ir    = device_dia / 2 + clearance
cradle_or    = cradle_ir + wall
barrel_len   = ring_h + device_depth + lip_thk
front_open_r = device_dia / 2 - lip_overlap
ring_or      = cradle_ir - ring_fit
ring_ir      = ring_or - wall
plate_dia    = cradle_or * 2

# ============================ HELPERS ============================
def cyl(r, h, z0=0.0):
    """Solid cylinder, base at z0."""
    m = cylinder(radius=r, height=h, sections=SEG)
    m.apply_translation([0, 0, z0 + h / 2])
    return m

def tube(ri, ro, h, z0=0.0):
    return difference([cyl(ro, h, z0), cyl(ri, h + 0.4, z0 - 0.2)], engine='manifold')

def wedge(r, z0, z1, a_start, a_sweep, steps=48):
    """Pie sector (radius r) from a_start..a_start+a_sweep deg, extruded z0..z1."""
    a0 = np.radians(a_start)
    a1 = np.radians(a_start + a_sweep)
    angs = np.linspace(a0, a1, max(2, int(steps * a_sweep / 90) + 1))
    pts = [(0, 0)] + [(r * np.cos(t), r * np.sin(t)) for t in angs]
    m = extrude_polygon(Polygon(pts), height=(z1 - z0))
    m.apply_translation([0, 0, z0])
    return m

def arc(r0, r1, z0, z1, a_start, a_sweep):
    """Annular wedge: r0..r1, z0..z1, a_start..+a_sweep (mirrors SCAD arc())."""
    ann = tube(r0, r1, z1 - z0, z0)
    w = wedge(r1 + 5, z0 - 0.5, z1 + 0.5, a_start, a_sweep)
    return intersection([ann, w], engine='manifold')

def rot_z(m, deg):
    m = m.copy()
    m.apply_transform(trimesh.transformations.rotation_matrix(np.radians(deg), [0, 0, 1]))
    return m

# ============================ PART 2: CRADLE ============================
def jslot():
    r0 = cradle_ir - 0.3
    r1 = cradle_or + 0.3
    slot_ang  = lug_w_deg + 4
    axial_top = lug_z + slot_w / 2 + 0.6
    axial = arc(r0, r1, -0.2, axial_top, -slot_ang / 2, slot_ang)
    circ  = arc(r0, r1, lug_z - slot_w / 2, lug_z + slot_w / 2, -slot_ang / 2, slot_ang + twist_deg)
    return union([axial, circ], engine='manifold')

def usb_cut():
    m = box(extents=(wall + 3, usb_w, usb_h))
    m.apply_translation([cradle_ir + wall / 2, 0, usb_from_back])
    return rot_z(m, usb_angle_deg)

def build_cradle():
    barrel = tube(cradle_ir, cradle_or, barrel_len)
    lip = tube(front_open_r, cradle_ir + 0.01, lip_thk, barrel_len - lip_thk)
    body = union([barrel, lip], engine='manifold')
    cuts = [rot_z(jslot(), i * 360 / lug_count) for i in range(lug_count)]
    cuts.append(usb_cut())
    return difference([body] + cuts, engine='manifold')

# ============================ PART 1: WALL PLATE ============================
def lug():
    return arc(ring_or - 0.1, ring_or + lug_protrude,
               lug_z - lug_h / 2, lug_z + lug_h / 2,
               -lug_w_deg / 2, lug_w_deg)

def screw_hole():
    shank = cyl(screw_d / 2, disc_thk + 0.4, -0.2)
    csk_h = (screw_head - screw_d) / 2 + 0.6           # countersink depth
    csk = trimesh.creation.cone(radius=screw_head / 2, height=csk_h, sections=SEG)
    csk.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))  # apex down
    csk.apply_translation([0, 0, disc_thk + 0.01])     # wide base flush at top face
    return union([shank, csk], engine='manifold')

def build_plate():
    disc = cyl(plate_dia / 2, disc_thk)
    ring = tube(ring_ir, ring_or, ring_h, disc_thk)
    # lug() is defined with z measured from the disc base; raise it onto the ring
    lugs = []
    for i in range(lug_count):
        l = rot_z(lug(), i * 360 / lug_count)
        l.apply_translation([0, 0, disc_thk])
        lugs.append(l)
    body = union([disc, ring] + lugs, engine='manifold')

    cuts = [cyl(cable_hole / 2, disc_thk + ring_h + 1, -0.1)]
    for i in range(screw_count):
        h = screw_hole()
        h.apply_translation([screw_bc / 2, 0, 0])
        cuts.append(rot_z(h, i * 360 / screw_count + 90))
    if side_notch:
        n = box(extents=(plate_dia / 2 + 2, cable_hole, disc_thk + 0.4))
        n.apply_translation([(plate_dia / 2 + 2) / 2, 0, disc_thk / 2 - 0.1])
        cuts.append(rot_z(n, 90))
    return difference([body] + cuts, engine='manifold')

# ============================ EXPORT ============================
if __name__ == "__main__":
    print("Building wall plate ...")
    plate = build_plate()
    plate.export("wall_plate.stl")
    print(f"  wall_plate.stl  watertight={plate.is_watertight}  "
          f"tris={len(plate.faces)}  bbox={np.round(plate.extents,1)}")

    print("Building cradle ...")
    cradle = build_cradle()
    cradle.export("cradle.stl")
    print(f"  cradle.stl      watertight={cradle.is_watertight}  "
          f"tris={len(cradle.faces)}  bbox={np.round(cradle.extents,1)}")
    print("Done.")
