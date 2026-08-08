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
    +X  right,  0 .. FACE_W      (240)
    +Y  up,     0 .. FACE_H      (136)
    +Z  toward the VIEWER,  z=0 is the REAR plane that sits against the wall

DEPTH STACK (z):
      0.0   rear outer plane (against the wall)
      2.5   interior floor            (REAR_WALL)
      3.0   rear-most component plane (+ REAR_CLR)
      9.85  PCB rear face             (+ REAR_COMP_H)  == boss tops
     18.0   face plate underside      == TOP OF THE SHELL
     20.5   glass front  ==  face plate front  (+ ENVELOPE)
    -> WRAP-AROUND: the module passes UP THROUGH the face plate, so the two front
       surfaces are coplanar. DEPTH is the glass plane, not glass + a bezel.

  !!! VALUES TO VERIFY WITH CALIPERS BEFORE A FINAL PRINT !!!
  * The USB-C breakout hole diameter / centres were scaled from a product photo.
  * RELIEF_D -- 2.0 mm of clearance for a plugged Crowtail cable is still a guess.
  Everything else here is measured: the board and its holes from Elecrow's Eagle
  files, the envelope and the module outline/position on the unit itself.
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

ENVELOPE     = 17.50    # MEASURED: glass front face -> rear-most component
                        # (re-measured; was 16.50, which made the case 1 mm too shallow)
REAR_COMP_H  = 6.85     # MEASURED (indirectly): with the board on 8.5 mm bosses the glass
                        # front read 3.85 below the 23.0 rim, i.e. z=19.15, so PCB-rear to
                        # glass is 10.65 and the rear stack is 17.5 - 10.65. Was 5.50, which
                        # sat the board 1.35 mm low -- and left the Crowtail connector
                        # reaching BELOW the floor, fitting only because it happened to sit
                        # over the I2C relief. That was luck, not design.
SD_PROUD     = 1.50     # MEASURED: microSD slot past the PCB rear face (inside ENVELOPE)

# --- Display module. MEASURED on the board, and it matches the standard 7" 1024x600 IPS
# --- panel spec exactly: outline 164.9 x 100.0, active area 154.21 x 85.92.
# --- Borders to the PCB edge measured 6 / 6 / 2 / 2, and 176.9-12 = 164.9,
# --- 104-4 = 100.0 -- both reconcile to the spec, so the part is identified.
GLASS_W      = 164.90   # module outline -- what the face opening must clear
GLASS_H      = 100.00
GLASS_X0     =   6.00   # MEASURED: PCB left edge -> module edge (front view)
GLASS_Y0     =   2.00   # MEASURED: PCB bottom edge -> module edge

AA_W         = 154.21   # active (lit) area. Elecrow's "155 x 87" is a rounding of this.
AA_H         =  85.92
# Active area centred in the module (standard for this panel family).
AA_X0        = GLASS_X0 + (GLASS_W - AA_W) / 2.0   # 11.345 from the PCB's LEFT edge
AA_Y0        = GLASS_Y0 + (GLASS_H - AA_H) / 2.0   #  9.040 from the PCB's BOTTOM edge
AA_MEASURED  = True

SCREEN_CLR   = 0.6      # total clearance on the face opening, around the module

# --- Front-view feature positions used by the case (see the spec's front-view table) ---
J10_X, J10_Y = 6.17, 19.80      # +5V_IN -- FAR LEFT, low
J13_X, J13_Y = 136.90, 96.57    # Crowtail I2C -- column side, high
SD_X,  SD_Y  = 159.68, 15.20    # microSD slot
BOOT_X, BOOT_Y = 172.62, 31.74  # BOOT  (right edge)
RST_X,  RST_Y  = 172.62, 17.25  # RESET (right edge)

# ---------------------------------------------------------------- case shell
FACE_W       = 240.0
FACE_H       = 136.0
CASE_R       = 14.0     # --case-radius token
WALL         = 3.0      # side wall thickness
REAR_WALL    = 2.5
FACE_T       = 2.5
CLR          = 0.75     # clearance around the PCB in X, on the COLUMN side
# The LEFT side is different. J10 -- the XH2.54 that feeds +5V_IN -- is a right-angle
# connector whose opening faces the board's edge, and in front view that edge is the LEFT
# one. So the mating housing plugs in horizontally and sticks out past the board: roughly
# 2 mm of housing beyond the shroud, plus wire exit and bend. 0.75 mm was nowhere near it.
# This costs WIDTH (230 -> 240), not depth -- the case is still 22 mm thick.
CLR_LEFT     = 10.0     # PCB left edge to the inside of the wall, for the J10 cable
CLR_Y        = 4.75     # ...and in Y. Wider on purpose: at 0.75 the board dropped in but
                        # sat hard against the magnet blocks above and below it, with no
                        # room to get a finger to it. 2 mm added, then 2 mm again -- the
                        # blocks were still pinching the board. Face 128 -> 132 -> 136.
REAR_CLR     = 3.5      # gap behind the tallest rear component.
                        # 0.5 was right for the BARE board, but the J10 power cable is the
                        # real rear-most feature once it is plugged: measured 22.5 mm from
                        # the glass front to the back of the wires, against a 17.5 mm bare
                        # envelope. At REAR_CLR 0.5 the cable ended 2.0 mm BEHIND the
                        # outside of the case -- it would have held the unit off the wall.
                        # Raising this lifts the board off the floor and, because the glass
                        # is pinned to the face by the wrap-around, deepens the case with it.
PLUGGED_DEPTH = 22.5    # MEASURED: glass front -> back of the wires in the plugged J10

# derived z planes
FLOOR_Z      = REAR_WALL                 #  2.5
COMP_Z       = FLOOR_Z + REAR_CLR        #  3.0  rear-most component plane
GLASS_Z      = COMP_Z + ENVELOPE         # 19.5  glass front plane
PCB_BACK_Z   = COMP_Z + REAR_COMP_H      #  8.5  PCB rear face == boss top
# WRAP-AROUND BEZEL: the face does not sit ON the glass, it sits AROUND it, and the two
# front surfaces are coplanar. So DEPTH is the glass plane, and the face plate occupies
# the FACE_T below it with the module passing up through its opening.
DEPTH        = GLASS_Z                   # 20.5 -- glass front == face front
FACE_Z0      = DEPTH - FACE_T            # 18.0 -- face underside == top of the shell
# *** The shell must STOP at FACE_Z0, not at DEPTH. *** It used to be built to full
# depth, so its 3 mm perimeter ring occupied the same 2.5 mm the face plate needed. The
# face could not drop in: it perched on the rim, held 2.5 mm proud, and its spigots
# stopped short of the magnets. That was the gap under the face.

# --- Bands above/below the PCB.  Both must be thick enough to carry a face-plate screw
# --- boss, because the boss runs the full interior height and CANNOT pass through the
# --- PCB.  At BAND_BOT = 6 the free strip below the PCB was only 3.75 mm and the bottom
# --- edge had no fixing at all for 215 mm.  The TOP band additionally carries the
# --- keyholes, so it needs the entry hole plus real drop travel on top of that.
BAND_BOT     = 10.0
BAND_TOP     = FACE_H - BAND_BOT - 2 * CLR_Y - PCB_H   # 12.5

# PCB placement in the face
PCB_X0       = WALL + CLR_LEFT           # 13.0
PCB_Y0       = BAND_BOT + CLR_Y          # 12.75
PCB_X1       = PCB_X0 + PCB_W            # 180.65
PCB_Y1       = PCB_Y0 + PCB_H            # 116.75

# ---------------------------------------------------------------- control column
COL_X0       = PCB_X1 + CLR              # 181.40
COL_X1       = FACE_W - WALL             # 227.00
COL_CX       = (COL_X0 + COL_X1) / 2.0   # 204.20
COL_W        = COL_X1 - COL_X0           #  45.60  (--u7-ctrl-col is 46)

DIAL_D       = 36.0     # --knob-dia: the CAP diameter, which sits proud ON the face
DIAL_CY      = 100.0    # column features ride up with the taller face
# The face hole only has to clear the encoder's Ø7.0 BUSHING -- not the cap. Sizing it to
# the cap (Ø37) left a 37 mm hole you could see into, with the Ø36 cap floating inside it.
# At Ø9 the cap overhangs by 13.5 mm all round and hides the opening completely.
DIAL_HOLE_D  = 9.0

BTN_D        = 13.0     # --btn-dia
BTN_CLR      = 0.4
BTN_GAP      = 9.0      # --btn-gap read as the GAP between caps, not the pitch:
BTN_PITCH    = BTN_D + BTN_GAP           # 22.0 -- a 9 mm *pitch* is impossible with
                                         # Ø13 caps; the token is self-inconsistent.
BTN_ROW_Y    = (66.0, 46.0)              # play/rooms on top, prev/next below

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
#
# *** ORIENTATION: ENTRY HOLE AT THE BOTTOM, SLOT RUNNING UP. ***
# This was built upside down once, so here is the reasoning. The screw is fixed in the
# wall. You offer the unit up, pass the screw head through the wide entry hole, then let
# the unit DOWN -- so relative to the case the screw travels UPWARD, out of the entry hole
# and into the slot. The slot's closed UPPER end is what bears the weight.
# Build it the other way and you would have to push the unit up to engage it, and gravity
# would walk the screw straight back out of the entry hole.
SCREW_SHANK  = 3.5      # wall screw shank
SCREW_HEAD   = 7.0      # wall screw head
KEY_ENTRY_D  = SCREW_HEAD + 0.6          # 7.6  head passes through here
KEY_SLOT_W   = SCREW_SHANK + 0.5         # 4.0  shank rides in here
KEY_ENTRY_CY = 125.5                     # entry-hole centre; the slot runs UP from here
KEY_DROP     = 5.5                       # how far the unit drops to lock. Sized so the
                                         # head relief stops clear of the PCB: the relief
                                         # sits at z 2.5-5.5 and the PCB's rear components
                                         # start at z 3.0, so it must not reach over them.
KEY_SLOT_TOP = KEY_ENTRY_CY + KEY_DROP   # closed, load-bearing end -- ABOVE the entry
KEY_X        = (50.0, 195.0)             # 145 mm apart
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
KNOB_TIP_Z      = DEPTH + 8.0                     # 8 mm proud of the face, whatever it is
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
MAG_SPIGOT_H   = 4.0     # free depth of the shell bore above its magnet
# *** THE SEATING PLANE IS THE RIM, NOT THE MAGNETS. ***
# The spigot used to be exactly as long as the free bore, so the face bottomed on the
# shell magnet at the same instant it touched the rim -- over-constrained, with zero
# slack. Every tolerance pushes the same way (blind bores print shallow, glue sits under
# both discs), so in practice the spigot lands first and the face HOVERS off the rim.
# The spigot is therefore held back by MAG_AIRGAP: the plate seats on the rim and the six
# blocks, and the magnets pull across a small designed gap rather than defining position.
MAG_AIRGAP     = 0.5     # designed gap between the two magnet faces when seated
# The BORE is the primary dimension -- it is sized to the shell magnet, which must stay
# located, so it cannot simply be opened up to loosen the spigot fit. The SPIGOT is
# derived from it instead.
MAG_BORE_D0    = MAG_D_SHELL + 0.6       # 8.6 -- Ø8 disc drops in with 0.30 mm a side
# Six spigots have to engage six bores at once, spread over 203 mm. Two separately
# printed parts routinely differ by 0.1-0.3% in scale -- 0.2 to 0.6 mm across that span --
# so a tight fit is over-constrained and the face would refuse to sit down. A magnet pair
# self-centres, so let the MAGNETS align the plate and leave the spigots as stops that
# only engage under a shove. Was 0.4 (0.20 a side), which the tolerance stack eats.
MAG_SPIGOT_FIT = 0.8     # diametral clearance -> 0.40 mm a side
MAG_BORE_D     = MAG_BORE_D0                     # 8.6 -- ONE diameter, top to bottom
MAG_SPIGOT_D   = MAG_BORE_D - MAG_SPIGOT_FIT     # 7.8
MAG_BLOCK_HW   = 6.0     # half-width of the shell block that carries the pocket
# *** The block must STOP SHORT of the board. ***
# It used to be drawn to PCB_Y0 / PCB_Y1 -- the board's own edges -- so it touched the
# board by construction, and raising CLR_Y just moved the board and the block together.
# Two case-height increases bought no gap at all. This is the gap, and it is measured
# from the board, not from the band.
MAG_BLOCK_GAP  = 2.0
MAG_MATE_Z     = FACE_Z0 - MAG_SPIGOT_H          # 14.0 -- SHELL magnet's front face
                                                 # (FACE_Z0, not GLASS_Z: since the
                                                 # wrap-around, those are 2.5 apart)
MAG_SPIGOT_Z0  = MAG_MATE_Z + MAG_AIRGAP         # 14.5 -- spigot tip / FACE magnet face

# Positions: in the two bands, NEVER over the PCB (the block runs the full interior
# height), and clear of the keyholes.
# The SPIGOT sets these, not the magnet. It descends to MAG_MATE_Z (15.5), and over the
# PCB that height is display glass -- so the spigot must stay off the PCB footprint
# entirely, while still leaving real wall thickness outboard of it.
MAG_X        = (25.0, 110.0, 228.0)
# Where each block actually ends -- MAG_BLOCK_GAP short of the board, not at its edge:
MAG_BLOCK_BOT_Y1 = PCB_Y0 - MAG_BLOCK_GAP        #  12.75
MAG_BLOCK_TOP_Y0 = PCB_Y1 + MAG_BLOCK_GAP        # 120.75
# ...and the bore centred in the material each block actually has, so the walls balance:
#   bottom  y 0 .. 12.75  (outer wall + block)  -> bore  2.08.. 10.68, walls 2.08 / 2.08
#   top     y 120.75 .. 136                     -> bore 124.08..132.68, walls 3.33 / 3.33
# Derived, so they cannot drift out of step with the board again.
MAG_Y_BOT    = MAG_BLOCK_BOT_Y1 / 2.0            #   6.375
MAG_Y_TOP    = (MAG_BLOCK_TOP_Y0 + FACE_H) / 2.0 # 128.375
MAGNETS      = [(x, y) for y in (MAG_Y_BOT, MAG_Y_TOP) for x in MAG_X]

# Pry notch: with six pairs meeting face to face there is real holding force, so the
# plate needs somewhere to get a fingernail or spudger under. Bottom edge, hidden once
# the unit is on the wall.
PRY_W        = 22.0
# NOT at the centre of the face: FACE_W/2 = 120 put the notch across the bottom magnet
# block at x=110, opening into its bore and stripping the spigot's wall over the top
# 2.5 mm. Sited between the x=110 and x=228 blocks instead.
PRY_X        = 160.0

# !!! MAGNET POLARITY !!!  Glue ALL shell magnets one way up and ALL face-plate magnets
# the other, so every pair attracts. Mark one pole with a marker before gluing -- the same
# rule as hardware/round-nest-2.8/wall/mount_params.py.

# ---------------------------------------------------------------- I2C connector relief
# J13 (Crowtail I2C) is on the PCB's REAR face and is the tallest thing back there -- it
# is what set the measured 16.5 mm envelope. That leaves only REAR_CLR (0.5 mm) between
# it and the interior floor, which is nothing once a plug is in it. Rather than deepen the
# whole case for one connector, the floor is relieved locally over J13 and the plug's exit
# path toward the PCB's top edge.
# Both reliefs are 1.5 mm into the floor -> 2.0 mm of total clearance, leaving 1.0 mm of
# rear wall. Defined RELATIVE to their connectors so they follow if the board moves.
RELIEF_D      = 1.5

# J13 (I2C), near the PCB's top edge: the plug exits toward that edge.
I2C_RELIEF    = (PCB_X0 + J13_X - 14.0, PCB_Y0 + J13_Y - 7.0,
                 PCB_X0 + J13_X + 14.0, PCB_Y1 + 2.0)

# J10 (power) is a right-angle connector opening toward the board's LEFT edge, so its
# housing and cable live in the CLR_LEFT gap. The relief runs from near the wall, under
# the connector body, and back far enough to clear the shroud.
J10_RELIEF    = (WALL + 0.5,       PCB_Y0 + J10_Y - 10.0,
                 PCB_X0 + 9.0,     PCB_Y0 + J10_Y + 10.0)

CABLE_CH_W   = 6.0      # channel across the rear for the J10 power pair
SEG          = 96       # circle smoothness
