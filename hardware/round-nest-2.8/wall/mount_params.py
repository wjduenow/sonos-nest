"""
Shared geometry for the Sonos knob wall mount — MAGNETIC pull-off version.

BOTH build_wall_plate.py and build_cradle_from_reference.py import this, so the
wall-plate and cradle interfaces can never drift apart.

How it mates (replaces the old twist-lock bayonet):
  * The wall plate is a disc with a shallow LIP ring. The cradle's cup rim drops
    INTO that lip — the lip centres the unit and carries its weight (shear), so it
    can't slide down off the magnets.
  * Disc magnets (glued in, face flush) on a bolt circle pull the two faces
    together. Pull straight off to remove (reboot), push back on to reseat — no
    twisting, which matters because the Ø79 front bezel rotates freely and you
    can't grip the hidden body to twist a bayonet.
  * One anti-rotation KEY (a tab on the cup that drops into a slot in the lip)
    indexes the unit upright and stops it creeping round. Removal is still a
    straight axial pull — the tab just slides out of the slot.

The Ø79 front bezel overhangs and hides the whole mount.

Magnet polarity: glue ALL wall-plate magnets one way and ALL cradle magnets the
other way, so every pair attracts. (Mark one pole with a marker before gluing.)
"""
# --- cup (from the reference mount; the wall-facing rim at z=0) ---
wall   = 3.0
fit    = 0.4          # gap so the cup nests freely inside the wall-plate lip
cup_or = 31.0         # cup outer radius at the z=0 wall-facing rim
cup_ir = 29.0         # cup inner radius at that rim

# board pocket barrel (the part of the cup the display body sits in)
barrel_ir = 24.5      # measured from reference_mount.stl
barrel_or = 26.5
cup_depth_extra = 2.0 # extend the barrel wall this much at the opening (deeper pocket)

# --- nesting lip on the wall plate (centres the cup + takes shear/weight) ---
lip_h     = 3.0
lip_ir    = cup_or + fit          # 31.4  cup drops inside this
lip_or    = lip_ir + wall         # 34.4
plate_dia = lip_or * 2            # 68.8  wall-plate outer diameter

# --- magnets (disc, glue-in; face flush at the mating plane) ---
magnet_d     = 6.0    # disc magnet diameter (N52 Ø6×3 are cheap + strong)
magnet_h     = 3.0    # disc magnet thickness
mag_clear    = 0.3    # pocket diametral clearance (glue gap)
mag_pocket_d = magnet_d + mag_clear      # 6.3
mag_pocket_h = magnet_h + 0.2            # 3.2  (magnet glued, sits ~flush)
magnet_count = 4
magnet_bc    = 48.0   # magnet bolt circle (R24) — inside the lip, clear of screws
magnet_ang0  = 45.0   # first magnet angle; 45/135/225/315 dodge the 3 display screws

# cradle magnet pads: local bosses that bulge INWARD from the thin cup rim so a
# Ø6 magnet has somewhere to live without poking through the 2 mm wall.
pad_ir    = magnet_bc / 2 - mag_pocket_d / 2 - 1.5   # inner reach of the pad (~19.4)
pad_w_deg = 26.0
pad_h     = mag_pocket_h + 0.4                        # 3.6  (stays below the web at z≈4)

# --- anti-rotation key: cup tab -> wall-plate lip slot (keeps the screen upright) ---
anti_rotation_key = True
key_ang_deg  = 270.0  # key angle; 270° dodges the cable notch (90°) and screws (0/180°)
key_w_deg    = 10.0   # tab angular width
key_slot_deg = 16.0   # slot is wider so the tab self-finds on the way in
key_h        = 3.0    # tab height (within the lip)
key_or       = lip_or - 0.6   # tab tip radius (rides in the lip wall)

cable_hole = 11.0     # centre cable pass-through in the wall-plate disc
SEG = 220             # circle smoothness
