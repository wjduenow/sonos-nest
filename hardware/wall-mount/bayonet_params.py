"""
Shared bayonet / twist-lock geometry for the Sonos knob wall mount.

BOTH build_wall_plate.py and build_cradle_from_reference.py import this, so the
wall-plate collar/lugs and the cradle skirt/J-slots can never drift apart.

The bayonet engages on the OUTSIDE of the cup: the wall plate has an outer collar
whose lugs point INWARD into J-slots on the cup's outer wall. (An inner ring
would collide with the screw web in the slim cradle — there's no room below the
web for one.) The Ø79 front bezel overhangs and hides the mount.
"""
mount_od = 61.0       # cup outer diameter (where the J-slots are cut)
wall     = 3.0        # wall thickness
fit      = 0.5        # gap so the cup slides into the collar

# --- cup (cradle) ---
cradle_or = mount_od / 2          # 30.5  cup outer radius (J-slots here)
cradle_ir = cradle_or - wall      # 27.5  cup inner radius

# --- collar (wall plate) wraps the cup from OUTSIDE ---
# The reference cup's lower wall measures up to ~R31.5 (Ø63) in the bayonet zone
# (it's wider than the nominal mount_od), so the collar must clear that.
cup_bayonet_or = 31.6             # measured max cup-outer where the collar grips
collar_ir = cup_bayonet_or + fit  # 32.1  collar inner radius (cup slides in)
collar_or = collar_ir + wall      # 35.1
plate_dia = collar_or * 2         # 70.2  outer diameter of the wall plate
slot_or   = cup_bayonet_or + 1.5  # cradle J-slots cut out to here (fully through the wall)

# --- twist-lock lug / slot ---
collar_h     = 8.0    # collar height (hosts the inward lugs)
lug_z        = 5.0    # lug centre height above the wall-plate disc
lug_h        = 3.5    # lug size along the axis
lug_w_deg    = 12.0   # lug angular width
lug_count    = 3
lug_protrude = 2.5    # how far the lug reaches inward from the collar (tip ~R29.6)
slot_w       = lug_h + 0.8   # axial slop of the J-slot around the lug
twist_deg    = 25.0   # rotation to lock

cable_hole = 11.0     # centre cable pass-through in the wall-plate disc
SEG = 220             # circle smoothness
