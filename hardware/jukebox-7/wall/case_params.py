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
    +Y  up,     0 .. FACE_H      (128)
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
FACE_H       = 132.0
DEPTH        = 22.0
CASE_R       = 14.0     # --case-radius token
WALL         = 3.0      # side wall thickness
REAR_WALL    = 2.5
FACE_T       = 2.5
CLR          = 0.75     # clearance around the PCB in X
CLR_Y        = 2.75     # ...and in Y. Wider on purpose: at 0.75 the board dropped in but
                        # sat hard against the magnet blocks above and below it, with no
                        # room to get a finger to it. 2 mm added top and bottom.
REAR_CLR     = 0.5      # gap behind the tallest rear component

# derived z planes
FLOOR_Z      = REAR_WALL                 #  2.5
COMP_Z       = FLOOR_Z + REAR_CLR        #  3.0  rear-most component plane
GLASS_Z      = COMP_Z + ENVELOPE         # 19.5  glass front plane
PCB_BACK_Z   = COMP_Z + REAR_COMP_H      #  8.5  PCB rear face == boss top

# --- Bands above/below the PCB.  Both must be thick enough to carry a face-plate screw
# --- boss, because the boss runs the full interior height and CANNOT pass through the
# --- PCB.  At BAND_BOT = 6 the free strip below the PCB was only 3.75 mm and the bottom
# --- edge had no fixing at all for 215 mm.  The TOP band additionally carries the
# --- keyholes, so it needs the entry hole plus real drop travel on top of that.
BAND_BOT     = 10.0
BAND_TOP     = FACE_H - BAND_BOT - 2 * CLR_Y - PCB_H   # 12.5

# PCB placement in the face
PCB_X0       = WALL + CLR                # 3.75
PCB_Y0       = BAND_BOT + CLR_Y          # 12.75
PCB_X1       = PCB_X0 + PCB_W            # 180.65
PCB_Y1       = PCB_Y0 + PCB_H            # 116.75

# ---------------------------------------------------------------- control column
COL_X0       = PCB_X1 + CLR              # 181.40
COL_X1       = FACE_W - WALL             # 227.00
COL_CX       = (COL_X0 + COL_X1) / 2.0   # 204.20
COL_W        = COL_X1 - COL_X0           #  45.60  (--u7-ctrl-col is 46)

DIAL_D       = 36.0     # --knob-dia: the CAP diameter, which sits proud ON the face
DIAL_CY      = 98.0     # column features ride up with the taller face
# The face hole only has to clear the encoder's Ø7.0 BUSHING -- not the cap. Sizing it to
# the cap (Ø37) left a 37 mm hole you could see into, with the Ø36 cap floating inside it.
# At Ø9 the cap overhangs by 13.5 mm all round and hides the opening completely.
DIAL_HOLE_D  = 9.0

BTN_D        = 13.0     # --btn-dia
BTN_CLR      = 0.4
BTN_GAP      = 9.0      # --btn-gap read as the GAP between caps, not the pitch:
BTN_PITCH    = BTN_D + BTN_GAP           # 22.0 -- a 9 mm *pitch* is impossible with
                                         # Ø13 caps; the token is self-inconsistent.
BTN_ROW_Y    = (64.0, 44.0)              # play/rooms on top, prev/next below

# ---------------------------------------------------------------- printed knob cap
# For the Bourns PEC11J-9215F-S0015: Ø6.0 shaft, D-flat over the last 5 mm at 4.5 across.
# Geometry above the face (face outer = DEPTH = 22.0):
#     22.0 .. 25.0  shaft is ROUND        -> bore must be round here
#     25.0 .. 30.0  shaft is FLATTED      -> bore is D here, and keys the cap
# The bore is therefore round for its first stretch and D above it. A D-bore all the way
# down could not pass over the round part of the shaft at all.
KCAP_D        = DIAL_D          # 36.0
KCAP_H        = 14.0            # --knob-height, proud of the face
KCAP_GAP      = 0.5             # underside sits this far above the face, so it cannot rub
KCAP_FIT      = 0.25            # diametral clearance on the bore
KCAP_SHAFT_D  = 6.0
KCAP_FLAT     = 4.5             # across the flat
KCAP_ROUND_H  = 3.5             # round section of the bore, from the underside up
KCAP_BORE_H   = 8.5             # total bore depth (shaft gives 7.5 -> 1 mm of air above)
KCAP_FLUTES   = 36              # knurl: scallops cut around the rim
KCAP_FLUTE_D  = 2.0
KCAP_CHAMFER  = 1.0             # top edge
KCAP_LEADIN   = 0.6             # chamfer at the bore mouth, for assembly

# ---------------------------------------------------------------- keyhole wall mount
# Two keyholes in the TOP band, spread wide so the unit cannot swing.  The band has
# no PCB behind it, so the captured screw head has the full interior depth to sit in.
SCREW_SHANK  = 3.5      # wall screw shank
SCREW_HEAD   = 7.0      # wall screw head
KEY_ENTRY_D  = SCREW_HEAD + 0.6          # 7.6  head passes through here
KEY_SLOT_W   = SCREW_SHANK + 0.5         # 4.0  shank rides in here
KEY_ENTRY_CY = 125.5                     # entry-hole centre, inside the top band
KEY_DROP     = 5.5                       # how far the unit drops to lock. Sized so the
                                         # head relief stops clear of the PCB: the relief
                                         # sits at z 2.5-5.5 and the PCB's rear components
                                         # start at z 3.0, so it must not reach over them.
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

# Mount: a SINGLE flat plate the board screws onto through its two post holes -- not a
# slot between two ribs.  One face to register against, two screws, and the board can be
# fitted or removed without springing anything.
UC_BOARD_SIDE = -1      # which face of the plate the board mounts on: +1 = toward the
                        # column's right wall, -1 = toward the screen. Flipping this moves
                        # ONLY the board and the plate; the port stays on UC_CX.
UC_PLATE_T   = 3.0      # plate thickness
UC_REC_OFF   = 3.2      # VERIFY: receptacle centreline, measured from the board's
                        # mounting (bare) face = PCB 1.6 + half the 3.15 receptacle body
UC_PILOT     = 2.6      # M3 self-tap pilot in the plate

# Derived plate/board X. The plate face the board registers against is UC_REC_OFF from the
# port centreline, on the opposite side to the board.
UC_FACE_X    = UC_CX - UC_BOARD_SIDE * UC_REC_OFF        # plate face
UC_PLATE_CX  = UC_FACE_X - UC_BOARD_SIDE * UC_PLATE_T / 2.0
UC_BOARD_X   = UC_FACE_X + UC_BOARD_SIDE * UC_T          # board's outer face
# Clear space on the screw-head side -- the screws run along X, so a driver has to come in
# from this side. This is the number to maximise if assembly feels cramped.
UC_ACCESS    = (COL_X1 - UC_BOARD_X) if UC_BOARD_SIDE > 0 else (UC_BOARD_X - COL_X0)

# ---------------------------------------------------------------- rotary encoder board
# Arduino Modulino Knob, SKU ABX00107.  Qwiic/I2C, default address 0x76 (software
# configurable) -- clears the GT911 at 0x5D and the unidentified 0x2F.  I2C pull-ups are
# NOT fitted by default, which is what we want: the CrowPanel's bus already has them.
KNOB_W       = 41.00    # datasheet
KNOB_H       = 25.36
KNOB_PCB_T   = 1.60     # +/- 0.2
KNOB_HOLE_D  = 3.20     # 4x
KNOB_HOLE_DX = 32.00    # horizontal hole spacing
KNOB_HOLE_DY = 16.00    # vertical
KNOB_PILOT   = 2.60     # M3 self-tap pilot

# Encoder: Bourns PEC11J-9215F-S0015, 15 PPR / 30 detents, momentary push switch.
#   shaft Ø6.0, D-flat over the last 5 mm, flat at 4.5 across
#   L1 = 15.0 total from the bushing flange to the tip; LB = 7.0 bushing (Ø7.0)
KNOB_SHAFT_D    = 6.0
KNOB_SHAFT_L1   = 15.0
KNOB_SHAFT_LB   = 7.0
KNOB_SHAFT_FLAT = 5.0
KNOB_BODY_H     = 6.5   # VERIFY: encoder body height above the Modulino PCB
KNOB_STACK_H    = KNOB_BODY_H + KNOB_SHAFT_L1     # 21.5 PCB face -> shaft tip

# The board is positioned by where we want the SHAFT TIP to land, not by the floor: the
# cap needs real engagement, and KNOB_BODY_H is the only estimate in the chain.  Measure
# "shaft tip above the Modulino PCB" once and KNOB_STACK_H fixes the standoffs for you.
KNOB_TIP_Z      = 30.0                            # 8 mm into a 14 mm-proud cap
KNOB_PCB_Z      = KNOB_TIP_Z - KNOB_STACK_H       # 8.5  standoff top / board front face
KNOB_STANDOFF_D = 6.0

# ---------------------------------------------------------------- button I/O expander
# Adafruit PCF8574 I2C GPIO Expander Breakout, STEMMA QT / Qwiic, product 5545.
# Outline and holes are exact, from Adafruit's own Eagle file:
#   github.com/adafruit/Adafruit-PCF8574-PCB -- "Adafruit PCF8574 QT.brd"
# 8 GPIO (we need 4), address 0x20 with A0/A1/A2 jumpers giving 0x20-0x27 -- clear of the
# GT911 (0x5D), the unidentified 0x2F and the Modulino Knob (0x76).
# Inputs idle high on a weak (~100 uA) internal source, so the buttons just switch to GND;
# no external pull-ups. The chip has an INT output if polling ever proves too costly.
EXP_W        = 25.40    # exact (1.0")
EXP_H        = 17.78    # exact (0.7")
EXP_THICK    = 4.60     # incl. the STEMMA QT connectors
EXP_HOLE_D   = 2.50     # 2x plated
EXP_HOLE_CC  = 20.32    # centre-to-centre (holes at x 2.54 and 22.86)
EXP_HOLE_DY  = -6.35    # hole row, relative to the board centre (2.54 up from the bottom)
EXP_PILOT    = 2.10     # M2.5 self-tap pilot
# Mounted FLAT on the floor beneath the button grid: the switch bodies hang down from the
# face plate at z~19.5, the expander lives at z 4.0-8.6, so they share XY but never Z.
EXP_CX       = COL_CX
EXP_CY       = sum(BTN_ROW_Y) / 2.0      # midway between the two button rows
EXP_PCB_Z    = FLOOR_Z + 1.5             # 4.0 -- clearance under the board for wiring

# Rear port relief: deliberately oversized with an outside funnel, so the hole drilled
# in the wall does not have to be placed precisely.
# Sized to the receptacle body (8.94 x 3.26), not to the plug: the plug's overmold stays
# out in the wall's cable hole and never enters this opening.
#
# WHICH AXIS IS WHICH -- this was wrong once, so it is spelled out. The board stands ON
# EDGE, its plane perpendicular to the wall, i.e. the plane containing Y and Z. The
# receptacle is mounted on that face, so its LONG axis (8.94) lies in the board's plane
# and therefore runs ALONG the column (Y). Its short axis (3.26) is the board's normal,
# across the column (X).
PORT_W       =  5.0     # across the column (X) -> clears the 3.26 depth, 0.87 mm a side
PORT_H       = 17.0     # along  the column (Y) -> clears the 8.94 length, 4.03 mm a side
PORT_FUNNEL  =  2.5     # 45 deg flare on the WALL side only, to forgive the drilled hole
                        # -> outer opening 10.0 x 22.0. Was 4.0, which flared the short
                        # axis to 16 and read as a much bigger hole than it needed to be.

# ---------------------------------------------------------------- assembly
BOSS_OD      = 7.0      # PCB mounting boss
BOSS_PILOT   = 2.6      # M3 self-tap pilot
# ---------------------------------------------------------------- magnetic face plate
# The face is held on by MAGNETS, not screws -- nothing breaks the front surface.
#
# Using the 6 x 2 discs, not the 8 x 2. The bands either side of the PCB are only 7.75 mm
# and 10.25 mm of free strip, and a pocket for an 8 mm disc leaves no wall at the bottom.
# Six 6 mm pairs in DIRECT contact hold far harder than eight-through-plastic would.
#
# The plate is 2.5 mm thick, so a 2.2 mm pocket sunk into it would leave a 0.3 mm skin.
# Instead each magnet sits in a SPIGOT that descends MAG_SPIGOT_H below the mating plane
# into a matching recess in the shell. That buys real material over the magnet (4.3 mm to
# the front face), lets the two magnets meet FACE TO FACE with no plastic between them,
# and -- the reason it is worth the complexity -- the six spigots REGISTER the plate, so
# it cannot slide. Magnets are weak in shear; the spigots take it instead.
# TWO SIZES, on purpose. The shell's bore has to be wide enough to swallow the face
# plate's spigot anyway, so putting a 6 mm disc down there left a stepped hole: you had to
# drop the magnet 4 mm through a Ø8.6 recess and hope it found a Ø6.3 pocket at the bottom.
# An 8 mm disc fills that same bore, so the shell side is now ONE straight Ø8.6 hole --
# drop the magnet in, it lands flat at the bottom, glue it. The face plate keeps a 6 mm
# disc, because its spigot is only Ø8.2 and cannot wall anything larger.
MAG_D_SHELL    = 8.0     # in the shell -- fills the bore, no step
MAG_D_FACE     = 6.0     # in the face plate's spigot
MAG_T          = 2.0     # both are 2 mm thick
MAG_POCKET_D   = MAG_D_FACE + 0.3        # face-plate pocket
MAG_POCKET_H   = MAG_T + 0.2
MAG_SPIGOT_H   = 4.0     # how far the face plate's boss drops into the shell
MAG_SPIGOT_D   = 8.2
MAG_SPIGOT_FIT = 0.4     # diametral clearance in the shell's receiving bore
MAG_BORE_D     = MAG_SPIGOT_D + MAG_SPIGOT_FIT   # 8.6 -- ONE diameter, top to bottom
MAG_BLOCK_HW   = 6.0     # half-width of the shell block that carries the pocket
MAG_MATE_Z     = GLASS_Z - MAG_SPIGOT_H          # 15.5 -- where the two magnets meet

# Positions: in the two bands, NEVER over the PCB (the block runs the full interior
# height), and clear of the keyholes at x = 45 / 185.
# The SPIGOT sets these, not the magnet. It descends to MAG_MATE_Z (15.5), and over the
# PCB that height is display glass -- so the spigot must stay off the PCB footprint
# entirely, while still leaving real wall thickness outboard of it.
MAG_X        = (20.0, 100.0, 218.0)
# Centred in the material available either side, so the Ø8.6 bore keeps balanced walls:
#   bottom, y 0..12.75 (wall + band)  -> bore 2.10..10.70,   walls 2.10 / 2.05
#   top,    y 116.75..132             -> bore 120.10..128.70, walls 3.35 / 3.30
MAG_Y_BOT    = 6.4
MAG_Y_TOP    = 124.4
MAGNETS      = [(x, y) for y in (MAG_Y_BOT, MAG_Y_TOP) for x in MAG_X]

# Pry notch: with six pairs meeting face to face there is real holding force, so the
# plate needs somewhere to get a fingernail or spudger under. Bottom edge, hidden once
# the unit is on the wall.
PRY_W        = 22.0
PRY_X        = FACE_W / 2.0

# !!! MAGNET POLARITY !!!  Glue ALL shell magnets one way up and ALL face-plate magnets
# the other, so every pair attracts. Mark one pole with a marker before gluing -- the same
# rule as hardware/round-nest-2.8/wall/mount_params.py.

# ---------------------------------------------------------------- I2C connector relief
# J13 (Crowtail I2C) is on the PCB's REAR face and is the tallest thing back there -- it
# is what set the measured 16.5 mm envelope. That leaves only REAR_CLR (0.5 mm) between
# it and the interior floor, which is nothing once a plug is in it. Rather than deepen the
# whole case for one connector, the floor is relieved locally over J13 and the plug's exit
# path toward the PCB's top edge.
I2C_RELIEF_D  = 1.5     # into the floor -> 2.0 mm total clearance, 1.0 mm of wall left
I2C_RELIEF_HW = 14.0    # half-width in X, around J13
I2C_RELIEF_Y0 = 102.0   # from below the connector...
I2C_RELIEF_Y1 = 118.5   # ...out past the PCB's top edge, where the plug and cable go

CABLE_CH_W   = 6.0      # channel across the rear for the J10 power pair
SEG          = 96       # circle smoothness
