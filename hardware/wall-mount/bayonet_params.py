"""
Shared bayonet / twist-lock geometry for the Sonos knob wall mount.

BOTH build_wall_plate.py and build_cradle_from_reference.py import this, so the
wall-plate ring/lugs and the cradle skirt/J-slots can never drift apart.

The mount is sized to the display's REAR body / the reference cup (~Ø61) -- NOT
the Ø79 front bezel, which overhangs and hides the mount. Change `mount_od` to
resize everything together.
"""
mount_od = 61.0       # outer diameter of both parts (matches the reference cup)
wall     = 3.0        # skirt / ring wall thickness
ring_fit = 0.4        # gap so the ring slides inside the skirt

# --- bayonet ring (wall plate) <-> skirt + J-slots (cradle) ---
cradle_or = mount_od / 2          # 30.5  skirt outer radius
cradle_ir = cradle_or - wall      # 27.5  skirt inner radius
ring_or   = cradle_ir - ring_fit  # 27.1  bayonet ring outer radius
ring_ir   = ring_or - wall        # 24.1  bayonet ring inner radius (open: screw access + cable)
plate_dia = mount_od              # 61

# --- slimmed standoff ---
ring_h    = 8.0       # wall-plate ring height (main standoff contributor)
lug_z     = 5.0       # lug centre height above the wall-plate disc
lug_h     = 3.5       # lug size along the axis
lug_w_deg = 12.0      # lug angular width
lug_count = 3
lug_protrude = 2.0    # how far the lug sticks out radially
slot_w    = lug_h + 0.8   # axial slop of the J-slot around the lug
twist_deg = 25.0      # rotation to lock

cable_hole = 11.0     # centre cable pass-through in the wall-plate disc
SEG = 220             # circle smoothness
