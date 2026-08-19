"""
Shared geometry for the `button-v2` case -- a Seeed XIAO ESP32S3 plus one FILN FLM12-FJ-6
pushbutton, in a box that double-sided-tapes to the UNDERSIDE of a nightstand (or hangs on
two screws via the lid's keyhole slots) with the button facing DOWN.

build_shell.py and build_lid.py both import this so the two parts can never drift apart.

SAME PRODUCT as ../../cam-button, on a board a quarter the size.  Firmware: the `button-v2`
env; see plans/11-button-v2.md.

THE BIG IDEA -- the board sits FLAT OVER THE BUTTON'S BACK.
../../cam-button could not do this: an ESP32-S3-CAM is 30.4 x 38.4 with 14.5 mm of
pre-soldered header stack, so "bore over the board" costed out at ~37 mm tall (plans/04 §6)
and it had to thread the button through the channel between the two header rows instead.
The XIAO is 17.78 x 20.96 with a COMPLETELY FLAT UNDERSIDE (verified below), so it simply
lies on top of the button's wire tail:

    cam-button, bore in the header channel   -> 26.81 mm tall, 48.2 x 44.2 floor, 57.1 cm^3
    button-v2,  board over the button        -> see HEIGHT below,  ~36.8 x 26.8,  ~26 cm^3

Note the height barely moves.  IT IS THE BUTTON, NOT THE BOARD: BUTTON_BEHIND_T (13.35) plus
the panel and the wire tail is 18.35 mm before the board contributes anything.  The board swap
buys FOOTPRINT -- less than half -- and that is the honest claim.

Board numbers are read out of the vendor's own STEP export unless marked otherwise:
    wiki.seeedstudio.com/xiao_esp32s3_getting_started ->
    files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/seeed-studio-xiao-esp32s3-3d_model.zip
    -> "XIAO-ESP32S3 v2.step"   (the file is in METRES; every number here is mm)
See ../README.md for how each was obtained and which ones still want a caliper.

    +X  board width   (17.78 mm axis)
    +Y  board length  (20.96 mm axis -- USB-C at -Y, u.FL antenna at +Y)
    +Z  UP, away from the button, toward the nightstand
        z = 0        outer face of the button panel (faces DOWN, at the user)
        z = HEIGHT   the TAPE FACE (faces UP, against the nightstand)

Board frame == world frame in X/Y (PCB centre at x=0, y=0); the PCB's back face sits at
world z = PCB_BACK_Z.  The STEP was rotated 180 deg about Z (a DETERMINANT +1 transform, not
a mirror -- a single-axis flip would silently swap the board's left and right) so that the
USB-C lands at -Y, matching ../../cam-button's convention.
"""

# ---------------------------------------------------------------- board (STEP-verified)
# Every Z in this block is measured from the PCB's BACK (bottom) face, which is the datum the
# case cares about -- the board lands on ledges at its back face.
PCB_W        = 17.78   # X  (STEP: -8.89..8.89.  Exactly 0.700", which is why sources that
                       #     round it to "17.5" disagree with ones that say "17.8".)
PCB_L        = 20.96   # Y  (STEP: -10.48..10.48)
PCB_T        = 1.25    # STEP: the board solid is exactly 0.00..1.25
PCB_CORNER   = 2.0     # ⚠️ NOT VERIFIED (no outline arc extracted). Cosmetic only: the pocket
                       # is looser than the PCB on every side.

# *** NO MOUNTING HOLES. *** That is the defining difference from every other board in this
# repo and it drives the whole retention scheme -- see ../README.md §3.

# *** THE UNDERSIDE IS COMPLETELY FLAT. ***  Checked, not assumed: the minimum Z of EVERY body
# in the STEP is 0.000, i.e. the PCB itself.  There is no bottom-side B2B connector on the plain
# XIAO ESP32S3 v2 (that is a Sense-kit part), and no JSTs like the ESP32-S3-CAM's J1/J2 that
# ../../cam-button had to dodge.  So the board can seat on plain ledges anywhere, and nothing
# under it needs clearance.  build_shell.py re-asserts this from BOTTOM_PARTS being empty.
BOTTOM_PARTS = {}      # deliberately empty -- see above

# Top-side parts, from the STEP.  Z is from the PCB BACK face (so it includes PCB_T).
COMP_Z_MAX   = 4.46    # tallest point on the board's top side = the USB-C shell.
                       # (next: the shield can at 3.25, then u.FL 2.50, BOOT/RESET 1.98.)

# The USB-C.  It OVERHANGS the PCB's -Y edge by 12.00 - 10.48 = 1.52 mm and pokes into the
# wall line, exactly as the ESP32-S3-CAM's did (1.4 mm) -- the notch has to make room for it.
USB_X0, USB_X1 = -4.46, 4.48     # 8.94 wide: a standard USB-C receptacle
USB_Y_EDGE   = -12.00            # its outermost face, 1.52 beyond the PCB edge
USB_Y_INNER  = -4.70             # how far INBOARD the connector body reaches -- the lid rib
                                 # must stay clear of this (it bears on the shield can only)
USB_Z0, USB_Z1 = 0.26, 4.46      # from the PCB back face -> cable axis at z ~2.36

# The RF shield can.  A big, flat, RIGID slab of metal with nothing on it -- which is what makes
# it the right thing for the lid rib to bear on.  Pressing on the u.FL or the USB-C instead would
# put the retention load straight into a connector's solder joints.
SHIELD_X0, SHIELD_X1 = -6.30, 6.30
SHIELD_Y0, SHIELD_Y1 = -3.77, 6.83
SHIELD_Z     = 3.25              # top face, above the PCB back

# The u.FL antenna connector: +Y end, offset to +X.
UFL_X0, UFL_X1 = 3.18, 6.28
UFL_Y0, UFL_Y1 = 7.26, 10.26
UFL_Z        = 2.50              # top of the bare connector; a mated plug adds ~2 mm

# BOOT / RESET, top side, flanking the USB-C. No access feature is modelled -- see the note
# at the bottom of this file.
BOOT_RESET_BOX = (-6.72, 6.73, -10.21, -7.59)

# ---------------------------------------------------------------- button: FILN FLM12-FJ-6
# THE SAME PART AS ../../cam-button, and every number here is copied from its button_params.py
# unchanged -- including the two that are still estimates.  If you caliper the button, fix it in
# BOTH files (they are worth ~4 mm of height each).
BUTTON_BODY_D  = 11.71   # measured thread major Ø (plans/04 §6)
BUTTON_BORE_D  = 12.0    # = thread + ~0.3.  FDM prints holes undersize -> test-coupon it.
BUTTON_HEAD_D  = 14.0    # datasheet ø14 flange; sits ON the outer face and hides the bore
BUTTON_NUT_AF  = 16.0    # datasheet hex ACROSS FLATS
BUTTON_NUT_AC  = BUTTON_NUT_AF / 0.8660254        # = 18.48 across corners -- the real
                                                  # keep-out radius on the inside face
BUTTON_CLR     = 0.5     # keep-out padding around the body against everything else

# *** MEASURED ON THE REAL BUTTON, 2026-08-19: 14.0 mm tip to tail, dome top -> back of the
# connector. ***  This is the number `plans/04` §7.1a and ../../cam-button's risk list #1 have
# been waiting for, and it settles them by contradicting BOTH readings of the datasheet.
#
#   datasheet as "13.35 = behind-panel, + 1.5 head"  -> overall would be 14.85   (0.85 too tall)
#   datasheet as "13.35 = overall"                   -> overall would be 13.35   (0.65 too short)
#   MEASURED                                         -> overall is 14.00
#
# It also folds in something the old parameterisation kept separate and guessed at twice: the
# measurement runs to the BACK OF THE CONNECTOR, so whatever hangs off the switch's rear is
# already inside this 14.0.  `BUTTON_TAIL_T` no longer has to reserve room for it.
#
# Deriving BEHIND_T from the overall rather than storing it is the point: it is now impossible
# for the head and the body to disagree with the thing that was actually calipered.
BUTTON_OVERALL_T = 14.0   # MEASURED -- dome top to the back of the connector

# ⚠️ THE ONE REMAINING UNKNOWN IN THIS BLOCK, and it maps 1:1 into case height: how far the dome
# stands proud of the panel's OUTER face.  The datasheet's 1.5 is used until someone calipers it.
# Every millimetre here is a millimetre off HEIGHT, because BEHIND_T is what is left of the 14.0.
BUTTON_HEAD_T   = 1.5

# ⚠️ NOTE THE DATUM, because it is not ../../cam-button's and mixing them double-counts the
# panel.  That file stores BUTTON_BEHIND_T measured from the panel's INNER face and builds the
# stack as PANEL_T + BEHIND_T + TAIL_T.  The measurement above starts at the dome, so subtracting
# the head gives the distance from the panel's OUTER face -- which already contains the panel.
# Hence a differently-named constant, and a stack that adds it to nothing but the tail.
BUTTON_INSIDE_T = BUTTON_OVERALL_T - BUTTON_HEAD_T        # = 12.50, DERIVED -- never type it
                                                          # how far the button reaches INTO the
                                                          # box, from the outer panel face (z=0)

# ⚠️ ESTIMATED (plans/04 §7.1b).  The threaded section looks ~4 mm long on the drawing.  Minus
# the nut, that is all the panel you get -- thinner than a normal printed wall, so the panel is
# LOCALLY THINNED at the bore and the nut seats in that relief.
BUTTON_THREAD_L = 4.0
BUTTON_NUT_T    = 2.0    # ⚠️ ESTIMATED -- an M12x0.75 thin nut. Caliper it with the button.
# What is left of the thread once the nut has taken its share -- i.e. the panel thickness the
# bore is allowed to have.  A button property, so it lives here rather than in the height block
# where ../../cam-button puts it: NUT_ACCESS_Z below needs it, and that is further up the file.
BUTTON_PANEL_T  = BUTTON_THREAD_L - BUTTON_NUT_T           # = 2.0

# Axial room BEHIND THE CONNECTOR for the four wires to leave it and turn 90 deg.
#
# Note what this is NOT any more.  ../../cam-button's TAIL_T is its single most expensive unknown
# because it is doing two jobs at once -- clearing whatever is on the button's back AND coiling
# ~135 mm of pigtail under the header pin tips -- and it has to guess at both.  Here the first
# job is inside the measured 14.0 above, and the second does not exist (no header pins, and the
# assembly instruction is to TRIM THE PIGTAIL TO ~40 mm before soldering).  So this is purely the
# bend radius of four limp 26 AWG strands, and 2.5 is generous for that.
BUTTON_TAIL_T   = 2.5

# Dead centre.  ../../cam-button offsets its bore +4.0 in Y to dodge a soldered battery JST;
# this board's underside is flat, so there is nothing to dodge and the button sits under the
# middle of the board.
BUTTON_CX = 0.0
BUTTON_CY = 0.0

# ---------------------------------------------------------------- shell
WALL        = 2.5     # side walls
BOTTOM_WALL = 3.0     # the button panel -- the face the user pushes
LID_T       = 3.0     # the TAPE FACE.  3.0 so an M3 countersink still leaves 1.7 under the
                      # head AND a keyhole slot's screw-head pocket has somewhere to live.
TOP_CLR     = 0.5     # air over the tallest top-side part (the USB-C shell)

PCB_Y_GAP   = 0.4     # cavity is PCB + this per side in Y -> the end walls locate the board
PCB_X_GAP   = 7.0     # ...and deliberately loose in X, to house the lid columns, the antenna
                      # and the button harness beside the board
OUT_R       = 3.0     # outer corner radius

# ⚠️ THE ORIENTATION IS FORCED, NOT CHOSEN.  The nut relief is Ø19.5 and the board is
# 17.78 x 20.96, so the board's LONG axis must run along the same axis the relief needs room
# in.  Turn the board 90 deg and the cavity's short side becomes 17.78 + gaps = 18.58 < 19.5 and
# the relief breaks through the wall.  check_clearances() proves this every build.
#
# ⚠️ ...AND THE CAVITY'S SHORT AXIS IS SET BY A TOOL, NOT BY THE BOARD.  Sizing IN_Y off the
# board alone gives 21.76, which fits the nut (18.48 across corners) with room to spare and is
# still WRONG: a 16 mm socket is ~22 mm across, so it misses by a quarter of a millimetre and
# the M12 nut can only be pinched up by hand.  That is not good enough here.  This button is
# pressed hundreds of times and every press torques the body; a hand-tight nut backs off, the
# button starts to rotate in its bore, and the four soldered wires wind up and break off.
# ../../cam-button never had to think about it -- its cavity is 39.2 in the short axis.
#
# So the short axis is max(board, socket).  It costs 2.24 mm of Y and ~8% of the volume, and it
# is the difference between a case you can assemble properly and one you cannot.
NUT_SOCKET_D  = 22.0    # a 1/4" drive 16 mm socket, measured across its OD
NUT_SOCKET_CLR = 1.0
IN_Y   = max(PCB_L + 2 * PCB_Y_GAP, NUT_SOCKET_D + 2 * NUT_SOCKET_CLR)   # = 24.00
IN_X   = PCB_W + 2 * PCB_X_GAP                     # = 31.78
OUT_X  = IN_X + 2 * WALL                           # = 36.78
OUT_Y  = IN_Y + 2 * WALL                           # = 29.00

# ...which only helps if the socket can actually REACH the nut, so every locating feature stops
# short of it.  Below this height the cavity is clear wall-to-wall; the ribs and end packers
# bridge over the gap (a ~20 mm span between two walls -- it prints without support).
#
# DERIVED, not typed.  It was 5.0 as a literal, which happened to equal the formula while the
# button was assumed 13.35 long -- so shortening the button would have left it silently 1 mm too
# generous rather than tracking the nut.  Constants that "happen to agree" are the ones that rot.
NUT_ACCESS_Z  = BUTTON_PANEL_T + BUTTON_NUT_T + 1.0     # 1 mm of air over the nut

# With the cavity now wider than the board in BOTH axes, the end walls no longer locate it, so
# they are packed inward to the PCB edge above NUT_ACCESS_Z.  Together with the X ribs this is
# the "captured-edge pocket": the board drops into a rectangle that is 0.4 mm loose all round.
Y_PACK_IN = PCB_L / 2 + PCB_Y_GAP                  # = 10.88, inner face of each end packer

# ---------------------------------------------------------------- THE HEIGHT (all derived)
# Nothing below is a free choice: every term is either a button number or a board number.
NUT_RELIEF_D     = BUTTON_NUT_AC + 1.0                     # = 19.5 flat seat for the nut
NUT_RELIEF_DEPTH = BOTTOM_WALL - BUTTON_PANEL_T            # = 1.0  local thinning at the bore

# The button reaches BUTTON_INSIDE_T in from the outer face and the wires turn behind it.  The
# panel is NOT added here -- it is already inside BUTTON_INSIDE_T (see its datum note above).
PCB_BACK_Z = BUTTON_INSIDE_T + BUTTON_TAIL_T                    # = 15.00
CEIL_Z     = PCB_BACK_Z + COMP_Z_MAX + TOP_CLR                  # = 23.31  (lid underside)
HEIGHT     = CEIL_Z + LID_T                                     # = 26.31  OVERALL

# The clear space under the board, around the button body: where the antenna and the button
# harness live.  On ../../cam-button the equivalent number (WIRE_Z) was only 5.81 because the
# header pins ate the rest; here the whole 15.35 is usable.
UNDER_BOARD_Z = PCB_BACK_Z - BOTTOM_WALL                        # = 15.35

# ---------------------------------------------------------------- board retention (NO SCREWS)
# The XIAO has no mounting holes, so the board is held by geometry: it DROPS onto four ledges
# at PCB_BACK_Z, is located laterally by two ribs in X and by the cavity end walls in Y, and is
# pressed down by a rib on the lid bearing on the RF shield can.
#
# The load that actually matters is not rattle -- it is somebody plugging a USB-C cable in.
# Without mounting holes that force goes into the connector's solder joints, so the shell also
# pinches the cable overmold (USB_STRAIN_*) and offers a zip-tie slot.  See ../README.md §3.
RIB_X       = PCB_W / 2 + 0.4      # = 9.29  inner face of each X locating rib
RIB_T       = 2.5                  # rib thickness (outward from RIB_X)
RIB_TOP_Z   = 2.0                  # how far the rib rises ABOVE the PCB's top face
LEDGE_L     = 4.0                  # ledge length in Y, at each of the board's four corners
LEDGE_W     = 1.5                  # how far each ledge reaches IN from RIB_X, under the board
LEDGE_Y     = PCB_L / 2 - LEDGE_L / 2 - 0.5        # ledge centre in Y

# The lid rib. Bears on the shield can (top at PCB_BACK_Z + SHIELD_Z), never on a connector.
LID_RIB_W   = 10.0    # X length -- well inside the can's 12.60
LID_RIB_T   = 2.5     # Y thickness
LID_RIB_Y   = (SHIELD_Y0 + SHIELD_Y1) / 2          # = 1.53, the can's centre
# Projection below the lid underside, sized for ~0.2 mm of interference so the board is held
# rather than merely covered.  PLA flexes that much over a 10 mm rib without complaint.
LID_RIB_H   = (CEIL_Z - (PCB_BACK_Z + SHIELD_Z)) + 0.2

# ---------------------------------------------------------------- lid columns
# On the ±X sides only: the -Y end is the USB notch and the +Y end is the antenna pocket.
# x = ±13.0 with r=3.5 spans 9.5..16.5, so each column is CLEAR of the PCB (|x| <= 8.89) and
# MERGED INTO the side wall (inner face at 15.89) -- which is what makes a 20 mm column rigid
# instead of a noodle.  Same reasoning, same screws, as ../../cam-button.
LID_POST_X   = 13.0
LID_POST_Y   = 7.0
LID_POST_OD  = 7.0
LID_POST_PILOT = 2.6            # M3 self-tapper -- same as cam-button and rec-2.8
LID_SCREW_LEN  = 10.0           # M3 x 10
LID_SCREW_D    = 3.4            # clearance hole through the lid
LID_SCREW_HEAD = 5.0            # Ø5.0 90° countersink -- FLAT head is mandatory on the tape face
LID_CSK_ANG    = 90.0

# ---------------------------------------------------------------- USB-C
# Permanent power, so size the hole for a real CABLE OVERMOLD, not the connector.  The notch is
# OPEN-TOPPED -- it runs from USB_SLOT_Z0 straight up to the rim and the lid caps it.  The
# overmold's axis sits at PCB_BACK_Z + 2.36 = 20.71, so any hole generous enough would break the
# ceiling anyway, and an open notch prints without a single bridge.
USB_SLOT_W  = 16.0    # X -- vs the 8.94 connector: room for a chunky overmold
# DERIVED for the same reason NUT_ACCESS_Z is.  This was the literal 15.5, which was right only
# while PCB_BACK_Z happened to be 18.35; shortening the button by 3 mm would have lifted the
# connector's underside to 15.26 and left the notch FLOOR above it -- i.e. a case that traps the
# cable against solid plastic.  build_shell.py's "USB notch floor -> connector underside" check
# does catch it, but a derived value means it never arises.
USB_SLOT_DROP = 3.0   # how far below the connector's underside the notch opens, for the overmold
USB_SLOT_Z0 = PCB_BACK_Z + USB_Z0 - USB_SLOT_DROP          # floor of the notch

# Strain relief.  A pair of soft ribs that pinch the overmold, plus a zip-tie slot through the
# wall beside the notch.  Without mounting holes this is the ONLY thing standing between a
# yanked cable and the USB-C's solder joints.
USB_STRAIN_W   = 13.0   # pinch width -- ~3 mm under USB_SLOT_W, so a 16 mm overmold is gripped
USB_STRAIN_T   = 1.6    # how far each pinch rib stands proud into the notch
TIE_SLOT_W     = 2.6    # a 2.5 mm zip tie
TIE_SLOT_H     = 1.6

# ---------------------------------------------------------------- antenna
# ⚠️ THIS BOARD HAS NO ONBOARD ANTENNA (unlike the XIAO ESP32C6 that was considered first, and
# unlike the ESP32-S3-CAM's etched trace).  The detachable u.FL antenna that ships with it is
# NOT optional, and it has to live in here.
#
# It goes UNDER the board at the +Y end, in the open annulus around the button: the u.FL is at
# +Y (STEP-verified, and Y is unaffected by the handedness question in the header), the pigtail
# drops through UFL_NOTCH in the +Y locating wall, and the antenna lies against the end wall.
# That puts it ~10 mm from the button's Ø14 metal body -- better than the cam-button's ~4 mm,
# which works.
UFL_NOTCH_W  = 4.0     # notch in the +Y cavity wall for the coax to pass down
UFL_NOTCH_D  = 2.0     # how deep it bites into the wall
UFL_NOTCH_Z  = 3.0     # height of the notch, measured down from PCB_BACK_Z
# Deliberately cut across the FULL WIDTH rather than at the u.FL's exact X: which side of the
# board the connector ends up on depends on which way round the board is dropped in, and a notch
# that only works one way is a notch that will be wrong half the time.
UFL_NOTCH_FULL_WIDTH = True

# ---------------------------------------------------------------- keyhole slots (lid)
# The lid is BOTH the tape face and, optionally, a two-screw hanger.  ../../cam-button argues
# hard against holes in the adhesive face (its BOOT/RESET note) and that argument still holds
# for holes that buy nothing -- but these buy a whole second mounting route, so they are in.
#
# Cost, as built rather than as guessed: 159 mm^2 of a 1067 mm^2 face = **14.9%** of the
# adhesive area (build_lid.py prints it).  That is a real bite, and it is the reason the slots
# are sized to a #8/M4 head and not to something bigger "just in case" -- every extra mm of
# KEYHOLE_HEAD_D costs adhesive that the tape mount actually needs.
#
# The pair sits ON the lid's X centreline, separated in Y, and each slot RUNS IN X.  That is
# the only arrangement this lid has room for, and it is forced from both sides:
#   * the lid is 26.76 in Y, so a Ø8 head plus edge margin caps |y| at about 7.4
#   * the lid screws are at (±13.0, ±7.0), so a slot centred at x=0 can only be ±7.5 long
#     before it runs into them
# Slots along Y instead would have to squeeze between the lid screws in X and end up ~11 mm
# apart -- a worse anti-rotation span than the 16 mm this gives.
#
# ⚠️ 16 mm between the two screws is a SHORT span for a 36.8 mm box: it hangs fine but it will
# rock if you push it sideways.  Tape is still the primary mount; these are the alternative,
# and they want the box's LONG axis vertical so gravity seats it down the slots.
KEYHOLE_ON      = True
KEYHOLE_HEAD_D  = 8.0     # clears a #8 / M4 pan head
KEYHOLE_SLOT_W  = 4.2     # shank slot -- M4 clearance
KEYHOLE_TRAVEL  = 7.0     # head-centre to slot-end slide distance
KEYHOLE_X       = 0.0     # slot centre in X; the slot runs ±(TRAVEL + HEAD_D)/2 from here
KEYHOLE_Y       = 8.0     # ±Y positions -> 16 mm apart

# ---------------------------------------------------------------- BOOT / RESET access
# NO ACCESS FEATURE, for exactly the reason ../../cam-button gives: both tacts are TOP-side
# (BOOT_RESET_BOX above), so the only face they could be poked through is the lid -- which is
# the adhesive face -- and only the FIRST USB flash needs download mode, done with the case
# open.  After that auto-reset works and /ota takes over.
#
# The keyhole slots are the one exception to "nothing else earns a hole in the tape face", and
# they earn it by replacing the tape entirely rather than by sitting alongside it.

SEG = 96              # cylinder smoothness
