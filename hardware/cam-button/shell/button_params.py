"""
Shared geometry for the `sonos-button` case -- a nulllab/emakefun ESP32-S3-CAM plus one
FILN FLM12-FJ-6 pushbutton, in a box that double-sided-tapes to the UNDERSIDE of a
nightstand with the button facing DOWN.

build_shell.py and build_lid.py both import this so the two parts can never drift apart.

THE BIG IDEA -- the button NESTS under the board, it does not stack beside or below it.
The ESP32-S3-CAM's two 8-pin headers (J4/J5) run down the LEFT and RIGHT edges of the
PCB with a wide empty channel between them.  The button's Ø12 body lives in that channel,
so button and board interleave in Z instead of stacking:

    bore BESIDE the board  -> ~23 mm tall, but ~60 mm of floor   (plans/04 §6)
    bore OVER  the board   -> ~37 mm tall                        (plans/04 §6)
    bore IN the channel    -> ~26 mm tall AND a 48 x 44 floor    <- THIS FILE

Board numbers below are read out of the vendor's own KiCad STEP export
(`esp32s3_cam_3d.zip` -> esp32s3_cam.step) unless marked otherwise.  See ../README.md
for how each number was obtained and which ones still need a caliper.

    +X  board width   (30.4 mm axis -- the USB-C short edge is at -Y)
    +Y  board length  (38.4 mm axis -- USB-C at -Y, PCB antenna at +Y)
    +Z  UP, away from the button, toward the nightstand
        z = 0        outer face of the button panel (faces DOWN, at the user)
        z = HEIGHT   the TAPE FACE (faces UP, against the nightstand)

Board frame == world frame in X/Y (PCB centre at x=0, y=0); the PCB's back face sits at
world z = PCB_BACK_Z.  The STEP's own origin is (77.91, -89.10); every board number here
has already had that subtracted.
"""

# ---------------------------------------------------------------- board (STEP-verified)
# From esp32s3_cam.step, body [0:1:1:36].  PCB solid spans z 0.00..1.60, so *every* Z in
# this block is measured from the PCB's BACK (bottom) face, which is the datum the case
# cares about -- the board lands on bosses at its back face.
PCB_W        = 30.4    # X  (STEP: 62.71..93.11)
PCB_L        = 38.4    # Y  (STEP: -108.30..-69.90)
PCB_T        = 1.60    # STEP: the board solid is exactly 0.00..1.60
PCB_CORNER   = 2.0     # R2 -- ⚠️ NOT VERIFIED (no outline arc in the STEP body). Only
                       # cosmetic here: the cavity is looser than the PCB on every side.

# 4x mounting holes.  ⚠️ plans/04 §6 guessed "M2 (Ø2.2), not M3" from pad photos --
# THE VENDOR CAD SAYS OTHERWISE.  Each corner has a Ø3.20 bore AND a concentric Ø6.40
# circle, both through the full 1.60 board thickness: a textbook M3 mounting pad.
HOLE_D       = 3.20    # M3 clearance
HOLE_KEEPOUT = 6.40    # Ø6.4 pad/keep-out -> boss OD must stay under this
HOLE_DX      = 24.0    # X spacing  (STEP: x = ±12.00)
HOLE_DY      = 32.0    # Y spacing  (STEP: y = ±16.00)

# The two 8-pin headers, from the Ø0.85 drill grid in the STEP.  THIS IS THE MEASUREMENT
# THE WHOLE DESIGN RESTS ON: the rows sit at x = ±13.97 (= 11 x 2.54 apart), 2.54 pitch,
# 8 holes each, spanning y = -11.19 .. +6.59.
HDR_X        = 13.97   # |x| of both header rows          (STEP, Ø0.85 x 16)
HDR_PITCH    = 2.54    # (STEP: 6.59, 4.05, 1.51, -1.03, -3.57, -6.11, -8.65, -11.19)
HDR_Y0       = -11.19  # J4.1 / J5.1 -- the pin NEAREST the USB-C (plans/04 §3: "pin 1 is
                       # at the BOTTOM").  Independently confirmed by this drill grid.
HDR_N        = 8
# A 2.54 header's plastic body is ~2.54 square, centred on the pins -> it reaches
# HDR_BODY/2 either side of the row.  This is what actually narrows the channel.
HDR_BODY     = 2.54    # ⚠️ ESTIMATED (standard 2.54 strip). The STEP has NO header model
                       # at all -- J4/J5 have no 3D shape assigned -- so the plastic width
                       # is the one channel number that is not from CAD.
# CHANNEL_W (derived below) is the clear space between the two header bodies.

# ⚠️ WHICH WAY THE PINS PROTRUDE IS STILL UNVERIFIED (plans/04 §7.3).  The STEP omits the
# headers, so CAD cannot answer it.  This file assumes PINS POINT DOWN -- i.e. into the
# button's half of the box, flanking it -- because that is the design intent, and because
# it is the pessimistic case for the space under the PCB.  If they point UP instead, the
# box gets ~8.5 mm TALLER and this layout should be revisited.
HEADER_PINS_DOWN = True

# Bottom-side parts, from the STEP.  Both are UNUSED by this product (everything wires to
# J4) but they are soldered on, so they are hard obstructions the button must dodge.
#   name: (x0, x1, y0, y1, depth below the PCB back face)
BOTTOM_PARTS = {
    "J1 batt PH2.0-2":  (-12.08, -5.20,  -8.10,  -1.70, 4.53),
    "J2 PH2.0-4":       ( -6.00,  6.00, -19.30, -10.69, 5.55),
}

# Top-side parts, from the STEP.  Z is from the PCB BACK face (so it includes PCB_T).
COMP_Z_MAX   = 4.96    # tallest point on the board's top side = USB1, the USB-C shell.
                       # (next: U9 4.68, B1/B2 4.17.)  NOTE this is measured from the PCB
                       # BACK face, not the top face -- the STEP's datum, and ours.
USB_X0, USB_X1 = -4.79, 4.79     # USB-C shell in X (dead centre on the -Y short edge)
USB_Y_EDGE   = -20.60            # it overhangs the PCB's -Y edge (-19.2) by 1.4 mm
USB_Z0, USB_Z1 = 0.80, 4.96      # from the PCB back face
BOOT_BOX     = (-9.15, -5.85, -17.22, -12.57)    # B2, top side, flanks the USB-C
RESET_BOX    = ( 5.95,  9.25, -17.22, -12.57)    # B1, top side
# ⚠️ The PCB antenna is an etched trace, so it does not appear in the STEP at all.  The
# +Y end of the board (y > ~14) is the only component-free strip, which is where it almost
# certainly lives -> keep that end free of dense geometry (plans/04 §6 "Antenna").
ANTENNA_KEEPOUT_Y = 14.0

# ⚠️ MEASURED BY HAND, not from CAD (plans/04 §3): the board's real tip-to-tip thickness,
# driven by the pre-soldered headers.  The STEP only models -5.55..+4.96 = 10.51 mm
# because it has no header geometry -- so this number, not the CAD, governs.
BOARD_STACK_T = 14.5
# -> how far the lowest thing (a header pin tip) hangs below the PCB back face:
BOARD_BELOW_T = BOARD_STACK_T - COMP_Z_MAX        # = 9.54

# ---------------------------------------------------------------- button: FILN FLM12-FJ-6
# M12 x 0.75 IP67 momentary, ø14 head, 16 mm hex nut, 4-wire 150 mm pigtail.
BUTTON_BODY_D  = 11.71   # measured thread major Ø (plans/04 §6)
BUTTON_BORE_D  = 12.0    # = thread + ~0.3.  FDM prints holes undersize -> test-coupon it.
BUTTON_HEAD_D  = 14.0    # datasheet ø14 flange; sits ON the outer face and hides the bore
BUTTON_NUT_AF  = 16.0    # datasheet hex ACROSS FLATS
BUTTON_NUT_AC  = BUTTON_NUT_AF / 0.8660254        # = 18.48 across corners -- the real
                                                  # keep-out radius on the inside face
BUTTON_CLR     = 0.5     # keep-out padding around the body against everything else

# ⚠️ DISPUTED (plans/04 §6 + §7.1a).  The datasheet dimensions the body 13.35 ±0.1 and the
# head 1.5.  If 13.35 is the OVERALL length then the true behind-panel depth is ~11.85.
# Nobody has calipered it.  13.35 is the SAFE choice -- it errs 1.5 mm tall rather than
# fouling the lid -- so that is the default.  Worth 1.5 mm of case height.
BUTTON_BEHIND_T = 13.35

# ⚠️ ESTIMATED (plans/04 §7.1b).  The threaded section looks ~4 mm long on the drawing.
# Minus the nut, that is all the panel you get -- thinner than a normal printed wall, so
# the panel is LOCALLY THINNED at the bore and the nut seats in that relief.
BUTTON_THREAD_L = 4.0
BUTTON_NUT_T    = 2.0    # ⚠️ ESTIMATED -- an M12x0.75 thin nut. Caliper it with the button.

# ⚠️ ESTIMATED, and the single most expensive unknown in this file (worth 3 mm, and up to
# 9 mm if it is wrong).  Axial room behind the button's back face for the 4 wires to exit
# and turn 90° into the annulus around the body.  3.0 is enough for four 26 AWG PTFE
# strands (~0.9 mm each) -- they are limp and turn in nothing.
#
#   IF the MX1.25 connector is actually ON THE BUTTON'S BACK (rather than on the far end
#   of the 150 mm pigtail, which is how "board-to-wire pigtail" normally reads), this must
#   become ~9.0 for the mated housing + bend.  The case still fits the 33 mm ceiling then
#   (31.8 mm) -- see ../README.md.  Nothing else in the design changes.
BUTTON_TAIL_T   = 3.0

# Where the bore goes.  x = 0 puts it dead centre in the header channel.  y is pushed
# +4.0 because the Ø12 body would otherwise foul J1 (the battery JST) -- build_shell.py
# asserts this rather than trusting the comment.  See ../README.md "Why y = +4".
BUTTON_CX = 0.0
BUTTON_CY = 4.0

# ---------------------------------------------------------------- shell
WALL        = 2.5     # side walls
BOTTOM_WALL = 3.0     # the button panel -- the face the user pushes
LID_T       = 3.0     # the TAPE FACE.  3.0 so an M3 countersink still leaves 1.7 under
                      # the head: the tape face must stay FLAT, so the lid screws sink
                      # flush rather than standing proud of the adhesive.
TOP_CLR     = 0.5     # air over the tallest top-side part (the USB-C shell)

PCB_Y_GAP   = 0.4     # cavity is PCB + this per side in Y -> the walls locate the board
PCB_X_GAP   = 6.4     # ...and deliberately loose in X, to house the lid posts and the
                      # J4 wiring beside the board
OUT_R       = 3.0     # outer corner radius

# derived footprint
OUT_X  = PCB_W + 2 * PCB_X_GAP + 2 * WALL          # = 48.2
OUT_Y  = PCB_L + 2 * PCB_Y_GAP + 2 * WALL          # = 44.2
IN_X   = OUT_X - 2 * WALL                          # = 43.2  -> cavity |x| <= 21.6
IN_Y   = OUT_Y - 2 * WALL                          # = 39.2  -> cavity |y| <= 19.6

# ---------------------------------------------------------------- THE HEIGHT (all derived)
# Read top to bottom this is the whole product.  Nothing below is a free choice: every
# term is either a button number or a board number, which is the point (plans/04 §6:
# "BUTTON_BEHIND_T and BOARD_STACK_T both belong in button_params.py *upstream* of the
# shell height, which should be derived ... rather than set independently").
BUTTON_PANEL_T   = BUTTON_THREAD_L - BUTTON_NUT_T          # = 2.0  panel left after the nut
NUT_RELIEF_D     = BUTTON_NUT_AC + 1.0                     # = 19.5 flat seat for the nut
NUT_RELIEF_DEPTH = BOTTOM_WALL - BUTTON_PANEL_T            # = 1.0  local thinning at the bore

PCB_BACK_Z = BUTTON_PANEL_T + BUTTON_BEHIND_T + BUTTON_TAIL_T   # = 18.35
CEIL_Z     = PCB_BACK_Z + COMP_Z_MAX + TOP_CLR                  # = 23.81  (lid underside)
HEIGHT     = CEIL_Z + LID_T                                     # = 26.81  OVERALL

BOSS_H     = PCB_BACK_Z - BOTTOM_WALL                           # = 15.35

# the clear space left under the header pin tips -- this is where the 150 mm of pigtail
# coils, around the button body.  build_shell.py asserts it is positive.
WIRE_Z     = (PCB_BACK_Z - BOARD_BELOW_T) - BOTTOM_WALL         # = 5.81

# the clear channel between the two header bodies -- the load-bearing number
CHANNEL_W  = 2 * HDR_X - HDR_BODY                               # = 25.40

# ---------------------------------------------------------------- board mount bosses
# SCREWS ARE DELIBERATELY THE SAME AS THE SLEEP-MACHINE (../../rec-2.8/countertop): M3
# self-tapping / thread-forming into bare printed plastic, Ø2.6 pilots, no nuts and no
# heat-set inserts.  That unit's board also has Ø3.2 corner holes, so the board screw is
# literally the same part -- see ../README.md "Screws (BOM)".
BOSS_OD    = 6.0      # < HOLE_KEEPOUT (6.4).  (rec-2.8 uses 5.4 under its Ø5.6 ring.)
BOSS_PILOT = 2.6      # M3 self-tapper -- same as rec-2.8's BOSS_PILOT
BOSS_RIB   = 2.0      # web thickness tying each boss to the nearest walls (they are
                      # 15.35 tall and Ø6 -- unsupported they are noodles)
BOARD_SCREW_LEN  = 8.0    # M3 x 8 flat-head, exactly as rec-2.8: ~6.4 mm of bite here
BOARD_SCREW_HEAD = 5.4    # keep the head inside the Ø6.4 keep-out ring
BOARD_SCREW_HEAD_H = 1.5  # low-profile flat head -- must clear the lid, asserted in build

# ---------------------------------------------------------------- lid posts
# On the ±X sides only: the -Y end is the USB notch and the +Y end is the antenna.
# x = ±19.5 with r=3.5 spans 16..23, so each column is CLEAR of the PCB (|x| <= 15.2) and
# MERGED INTO the side wall (inner face at 21.6) -- which is what makes a 21 mm column
# rigid instead of a noodle.
LID_POST_X   = 19.5
LID_POST_Y   = 14.0
LID_POST_OD  = 7.0
LID_POST_PILOT = 2.6            # M3 self-tapper -- same as rec-2.8's POST_PILOT
LID_SCREW_LEN  = 10.0           # M3 x 10, the same length rec-2.8 uses on its bezel
LID_SCREW_D    = 3.4            # clearance hole through the lid
# rec-2.8 models a Ø5.0 countersink and notes "a 90° flat head seats flush if you prefer".
# Here flush is NOT a preference: this face is the ADHESIVE face, so the FLAT head is
# mandatory and the countersink is the same Ø5.0 seat.
LID_SCREW_HEAD = 5.0
LID_CSK_ANG    = 90.0

# ---------------------------------------------------------------- USB-C
# Permanent power, so size the hole for a real CABLE OVERMOLD, not the connector.  The
# notch is OPEN-TOPPED -- it runs from USB_SLOT_Z0 straight up to the rim and the lid caps
# it.  That is deliberate: the overmold's axis sits ~21 mm up, so any hole generous enough
# to clear it would break through the ceiling anyway, and an open notch prints without a
# single bridge.
USB_SLOT_W  = 16.0    # X -- vs the 9.58 connector: room for a chunky overmold
USB_SLOT_Z0 = 16.0    # floor of the notch (the connector's own underside is at 19.15)

# ---------------------------------------------------------------- BOOT / RESET access
# Free to add and it saves a disassembly during bring-up (plans/04 §6).  Both tacts are on
# the TOP side flanking the USB-C, so they are poked through the LID.
BOOT_PIN_D  = 3.0
RESET_PIN_D = 3.0

# ---------------------------------------------------------------- wire retainers
# The pigtail is 150 mm and the button-to-J4 run is ~14 mm, so ~135 mm has to live
# somewhere.  It coils around the button body: the annulus between the Ø13 keep-out and
# the walls is ~69 mm around at r=11, so two turns swallow it, and it all sits BELOW the
# header pin tips (WIRE_Z = 5.81 mm of headroom).  These two posts just stop the coil
# springing up into the board.  Kept on +X: J1 is on -X.
WIRE_POSTS = [(10.5, -6.0), (10.5, -13.0)]
WIRE_POST_D = 4.0
WIRE_POST_H = 5.0

SEG = 96              # cylinder smoothness
