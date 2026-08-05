"""
Shared geometry for the sonos-jukebox 7" FLUSH WALL CASE.

build_shell.py and build_face.py both import this, so the two halves can never
drift apart.  Board numbers come from Elecrow's own Eagle files -- see
../crowpanel-p4-7-physical-spec.md, which is the source of truth and explains
every value here.

    !!! THE BOARD IS MIRRORED relative to the vendor Eagle files !!!
    Facing the screen, the USB-C ports and the 5 V input are on the LEFT.
    All X values in THIS file are already in FRONT-VIEW space
    (X_front = 176.90 - X_eagle).  Do not mix in raw Eagle X values.

WORLD FRAME (front view, looking at the screen):
    +X  right,  0 .. FACE_W      (230)
    +Y  up,     0 .. FACE_H      (124)
    +Z  toward the VIEWER,  z=0 is the REAR plane that sits against the wall

DEPTH STACK (z):
      0.0   rear outer plane (against the wall)
      2.5   interior floor            (REAR_WALL)
      3.0   rear-most component plane (+ REAR_CLR)
     19.5   glass front plane         (+ ENVELOPE 16.5, measured)
     22.0   face plate outer surface  (+ FACE_T)
    -> lands exactly on the design system's --u7-depth: 22mm token.

  !!! VALUES TO VERIFY WITH CALIPERS BEFORE A FINAL PRINT !!!
  * AA_X0 / AA_Y0 -- where the LIT area sits on the PCB.  NOT MEASURED YET.
    Defaulted to CENTRED, which is a guess.  This is the single value that moves
    the screen opening, so the face plate is provisional until it is measured.
  * REAR_COMP_H -- PCB rear face to the rear-most component (the Crowtail I2C
    connector).  Only the BOSS HEIGHT depends on it; everything else is
    referenced off the glass plane, which is measured.
  * The USB-C breakout hole diameter / centres were scaled from a product photo.
"""

# ---------------------------------------------------------------- board (from Eagle)
PCB_W        = 176.90   # X
PCB_H        = 104.00   # Y
PCB_T        = 1.65     # MEASURED
PCB_CORNER   = 3.00     # R3 board corners
HOLE_D       = 3.20     # 4x M3.2 through
HOLE_INSET   = 3.00     # hole centres, in from each PCB corner
HOLE_DX      = 170.90   # bolt pattern X
HOLE_DY      = 98.00    # bolt pattern Y

ENVELOPE     = 16.50    # MEASURED: glass front face -> rear-most component
REAR_COMP_H  = 5.50     # VERIFY: PCB rear face -> rear-most component
SD_PROUD     = 1.50     # MEASURED: microSD slot past the PCB rear face (inside ENVELOPE)

AA_W         = 155.00   # active (lit) area
AA_H         =  87.00

# --- Where the lit area sits on the PCB.  *** NOT MEASURED -- CENTRED GUESS *** ---
AA_X0        = (PCB_W - AA_W) / 2.0      # 10.95 from the PCB's LEFT edge  (front view)
AA_Y0        = (PCB_H - AA_H) / 2.0      #  8.50 from the PCB's BOTTOM edge
AA_MEASURED  = False                      # flip to True once caliper'd

# --- Front-view feature positions used by the case (see the spec's front-view table) ---
J10_X, J10_Y = 6.17, 19.80      # +5V_IN -- FAR LEFT, low
J13_X, J13_Y = 136.90, 96.57    # Crowtail I2C -- column side, high
SD_X,  SD_Y  = 159.68, 15.20    # microSD slot
BOOT_X, BOOT_Y = 172.62, 31.74  # BOOT  (right edge)
RST_X,  RST_Y  = 172.62, 17.25  # RESET (right edge)

# ---------------------------------------------------------------- case shell
FACE_W       = 230.0
FACE_H       = 124.0
DEPTH        = 22.0
CASE_R       = 14.0     # --case-radius token
WALL         = 3.0      # side wall thickness
REAR_WALL    = 2.5
FACE_T       = 2.5
CLR          = 0.75     # clearance around the PCB in its pocket
REAR_CLR     = 0.5      # gap behind the tallest rear component

# derived z planes
FLOOR_Z      = REAR_WALL                 #  2.5
COMP_Z       = FLOOR_Z + REAR_CLR        #  3.0  rear-most component plane
GLASS_Z      = COMP_Z + ENVELOPE         # 19.5  glass front plane
PCB_BACK_Z   = COMP_Z + REAR_COMP_H      #  8.5  PCB rear face == boss top

# --- bands above/below the PCB.  Asymmetric on purpose: the TOP band carries the
# --- keyholes and needs room for the entry hole plus real drop travel.
BAND_BOT     = 6.0
BAND_TOP     = FACE_H - BAND_BOT - 2 * CLR - PCB_H     # 12.5

# PCB placement in the face
PCB_X0       = WALL + CLR                # 3.75
PCB_Y0       = BAND_BOT + CLR            # 6.75
PCB_X1       = PCB_X0 + PCB_W            # 180.65
PCB_Y1       = PCB_Y0 + PCB_H            # 110.75

# ---------------------------------------------------------------- control column
COL_X0       = PCB_X1 + CLR              # 181.40
COL_X1       = FACE_W - WALL             # 227.00
COL_CX       = (COL_X0 + COL_X1) / 2.0   # 204.20
COL_W        = COL_X1 - COL_X0           #  45.60  (--u7-ctrl-col is 46)

DIAL_D       = 36.0     # --knob-dia
DIAL_CLR     = 1.0      # running clearance around the cap
DIAL_CY      = 92.0

BTN_D        = 13.0     # --btn-dia
BTN_CLR      = 0.4
BTN_GAP      = 9.0      # --btn-gap read as the GAP between caps, not the pitch:
BTN_PITCH    = BTN_D + BTN_GAP           # 22.0 -- a 9 mm *pitch* is impossible with
                                         # Ø13 caps; the token is self-inconsistent.
BTN_ROW_Y    = (58.0, 38.0)              # play/rooms on top, prev/next below

# ---------------------------------------------------------------- keyhole wall mount
# Two keyholes in the TOP band, spread wide so the unit cannot swing.  The band has
# no PCB behind it, so the captured screw head has the full interior depth to sit in.
SCREW_SHANK  = 3.5      # wall screw shank
SCREW_HEAD   = 7.0      # wall screw head
KEY_ENTRY_D  = SCREW_HEAD + 0.6          # 7.6  head passes through here
KEY_SLOT_W   = SCREW_SHANK + 0.5         # 4.0  shank rides in here
KEY_ENTRY_CY = 120.0                     # entry-hole centre (band spans 111.5..124)
KEY_DROP     = 6.5                       # how far the unit drops to lock
KEY_X        = (45.0, 185.0)             # 140 mm apart
KEY_HEAD_CLR = 3.0                       # clear depth kept behind the slot for the head

# ---------------------------------------------------------------- USB-C breakout
# SparkFun-pattern "USB C Breakout" v10.  Stands ON EDGE at the bottom of the control
# column, its PCB plane perpendicular to the wall, receptacle pointing at the wall.
#   21.4 axis -> vertical (Y) ; 14.5 axis -> depth (Z) ; 4.75 -> thickness (X)
#
# DATUM: the receptacle MOUTH sits at z = 0, flush with the rear plane that meets the
# wall, so the receptacle nests INTO the 2.5 mm rear wall instead of competing with it.
# The plug's nose then travels ~6.5 mm into the receptacle inside the case while its
# overmold stays in the wall's cable hole -- which is how the cable "pushes back in".
# Seating the board on the interior floor instead would recess the mouth 2.5 mm and
# force the overmold into the port cutout before the plug could seat.
#   0.0 receptacle mouth -> 14.5 board rear edge -> 19.5 face plate: 5.0 mm spare.
UC_H         = 21.4     # MEASURED
UC_D         = 14.5     # MEASURED: receptacle front face -> rear board edge
UC_T         = 4.75     # MEASURED: overall thickness (PCB + receptacle)
UC_HOLE_D    = 3.5      # VERIFY (photo-scaled)
UC_HOLE_CC   = 16.85    # VERIFY (photo-scaled; = 13.3 inside-edge + Ø3.5)
UC_HOLE_OFF  = 4.2      # VERIFY (photo-scaled): hole centres back from the receptacle face
UC_CX        = COL_CX   # 204.20
UC_CY        = 17.0     # clear of the lower button row (which reaches down to 31.3)
UC_SLOT_CLR  = 0.4      # slot clearance on the board thickness
UC_Z0        = 0.0      # receptacle mouth plane == rear outer plane (see above)
UC_Z1        = UC_Z0 + UC_D              # 14.5  board rear edge
UC_HEADROOM  = GLASS_Z - UC_Z1           #  5.0  spare to the face plate

# Rear port relief: deliberately oversized with an outside funnel, so the hole drilled
# in the wall does not have to be placed precisely.
PORT_W       = 14.0
PORT_H       =  8.0
PORT_FUNNEL  =  4.0     # 45 deg chamfer grown outward on the wall side

# ---------------------------------------------------------------- assembly
BOSS_OD      = 7.0      # PCB mounting boss
BOSS_PILOT   = 2.6      # M3 self-tap pilot
FSCREW_PILOT = 2.6      # face-plate screw pilot
FSCREW_HEAD  = 5.6
FSCREW_INSET = 7.0      # face screws, in from the face corners

CABLE_CH_W   = 6.0      # channel across the rear for the J10 power pair
SEG          = 96       # circle smoothness
