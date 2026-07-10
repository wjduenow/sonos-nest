"""
Shared geometry for the Hosyond / LCDWIKI ES3C28P 2.8" ESP32-S3 display
nightstand STAND CASE.  build_shell.py and build_bezel.py both import this so the
two parts can never drift apart.

Board is the ES3C28P (SKU on the QDtech "LCM OUTLINE" drawing, V1.0 2025-06-11).
All board numbers below are taken straight from that drawing / the LCDWIKI wiki:

    PCB            86.0 (H) x 50.0 (W) x 1.62 mm (measured), corner radius R3.5
    mount holes    4x Ø3.2, on a 42 x 78 mm rectangle  -> 4.0 mm in from each edge
                   (Ø5.6 keep-out ring around each: screw heads <= ~5.6 are safe)
    front glass    protrudes 4.38 mm above the PCB (6.00 stack - 1.62 PCB, measured);
                   glass 70.0 x 50.0 -- runs the full board height, edge to edge
    active area    43.20 x 57.60 ; viewing area 43.60 x 58.05
    back parts     up to 4.70 mm tall ; total module thickness 10.60 mm
    connectors     USB-C + RESET + BOOT on ONE short (50 mm) edge
    RESET / BOOT   tact buttons on the BACK face near that edge
    microSD        push-push socket on the back; its card MOUTH is flush with the -Y
                   (bottom, 86 mm) long edge -- the card enters from beyond that edge

MOUNTING: LANDSCAPE (86 mm wide, 50 mm tall), reclined 20deg from vertical.
Screen faces up-and-out for reading on a nightstand.

    +X  board width  (86 mm, horizontal)   -> the USB-C short edge is at -X
    +Y  depth        (toward the back of the stand)
    +Z  height       (up from the nightstand surface)

The board sits in a pocket on the reclined front face.  Local board-frame Z is
the screen normal: z=0 is the PCB front plane (== the face surface), +z points out
toward the viewer (glass lives here), -z goes back into the body (components here).

  !!! POSITIONS TO VERIFY WITH CALIPERS ON A REAL BOARD !!!
  The USB-C / RESET / mic positions along X (USB_Y, RESET_X, MIC_X) are best estimates
  from the board photos, NOT dimensioned on the outline drawing.  (RESET_Y and MIC_Y ARE
  measured: RESET is 7.0 mm c-to-c from the (-39, +21) corner hole; the mic is 8.0 mm up
  from the PCB's bottom (-Y) edge.)  The cut-outs
  are drawn generously and are single-line parameters here -- measure the actual board
  and nudge these before the final print.  render_preview.py plots them on a board map
  so you can eyeball the alignment.  microSD needs no verification: no case opening.
"""

# ---------------------------------------------------------------- board (verified)
PCB_W       = 86.0     # X, landscape width
PCB_H       = 50.0     # Y, landscape height (up the incline)
PCB_T       = 1.62     # MEASURED (drawing said 1.6)
PCB_CORNER  = 3.5      # R3.5 board corner radius
HOLE_D      = 3.2      # Ø3.2 mounting holes
HOLE_DX     = 78.0     # hole spacing along X (the 86 mm axis)
HOLE_DY     = 42.0     # hole spacing along Y (the 50 mm axis)
HOLE_KEEPOUT = 5.6     # Ø5.6 no-component ring -> boss OD stays under this
COMP_H      = 4.70     # tallest back-side component
# MEASURED: board+screen stack is 6.00 from the PCB back face to the glass top, and the
# PCB is 1.62 -> the glass stands 4.38 proud of the PCB front face.
GLASS_PROUD = 6.00 - PCB_T             # = 4.38  (drawing said 4.30)
GLASS_W     = 70.0     # MEASURED glass along X (landscape); drawing said 69.2
GLASS_H     = 50.0     # glass runs the full board height in Y, edge to edge
VA_W        = 58.05    # viewing area, landscape (X x Y)
VA_H        = 43.60
AA_W        = 57.60    # active area, landscape
AA_H        = 43.20

# ---------------------------------------------------------------- stand geometry
TILT_DEG    = 20.0     # recline from vertical (0 = upright)
MB          = 9.0      # face margin below the board (bottom lip)
MT          = 9.0      # face margin above the board (top lip)
FRONT_LIP   = 0.0      # face bottom sits at the very front-bottom of the base
W_OUT       = 94.0     # body width in X  -> ~3.6 mm side walls around the pocket
BACK_Y      = 48.0     # back wall depth (base footprint 0..BACK_Y)
                       # >=45.2 is REQUIRED by the 26 mm-deep speaker pocket: its front
                       # wall must clear the board-cavity floor plane (y = 0.364z + 8.833),
                       # else the pocket breaks into the cavity at the board's bottom edge
                       # -- exactly where the microSD socket + card live.
WALL        = 3.0      # nominal wall thickness target

# pocket that the board drops into (board footprint + clearance)
FIT         = 0.4      # per-side clearance around the PCB in the pocket
BACK_CLR    = 2.0      # gap behind the tallest component to the pocket floor
FACE_LEN    = PCB_H + MB + MT           # 66 mm reclined front face length

# board-mount bosses (self-tapping) at the 4 corner holes
BOSS_OD     = 5.4                        # < Ø5.6 keep-out
BOSS_PILOT  = 2.6                        # pilot for an M3 self-tapping screw
BOARD_SCREW_HEAD = 5.4                   # keep heads within the keep-out ring
BOARD_SCREW_HEAD_H = 2.4                 # head stand-off above the PCB front face

# bezel (screwed-on front cover).  The GLASS SITS FLUSH WITH THE BEZEL TOP: the bezel is
# exactly GLASS_PROUD thick and its opening is the glass outline (+GLASS_CLR/side), so the
# screen passes through the frame and the two surfaces end level.  There is NO raised rim
# any more -- the bezel lands directly on the shell face and on the bare PCB strips at
# |x| > GLASS_W/2, which also clamps the board down.
#
#   z = GLASS_PROUD ---- bezel top == glass top (flush)
#   z = 0 -------------- shell face == PCB front face == bezel underside
#
# The 4 board screws stand BOARD_SCREW_HEAD_H proud of the PCB front and now live UNDER
# the bezel, so the bezel underside gets a blind relief pocket over each one.
BEZEL_T     = GLASS_PROUD                # = 4.38 -> top is flush with the glass
BEZEL_OUT_X = W_OUT / 2                  # bezel outer == shell face outer -> flush
BEZEL_OUT_Y = FACE_LEN / 2
BEZEL_R     = 4.0                        # bezel outer corner radius
RIM_H       = 0.0                        # no raised rim (kept at 0 so pilots datum off it)
GLASS_CLR   = 0.5                        # bezel opening = glass outline + this per side
HEAD_RELIEF_D     = BOARD_SCREW_HEAD + 0.6   # Ø6.0 blind pocket over each board screw head
HEAD_RELIEF_DEPTH = BOARD_SCREW_HEAD_H + 0.2  # 2.6 deep -> leaves 1.78 of bezel above it
POST_X      = 39.0                       # bezel screws land in the top/bottom face margin
POST_Y      = 30.0
POST_PILOT  = 2.6                        # M2.5/M3 self-tapping bezel screws
POST_DEPTH  = 7.0                        # pilot depth into the face (bottom posts sit
                                         # close to the base -- 8.5 would nearly break out)
BEZEL_SCREW_HEAD = 5.0                   # countersink head Ø

# ---------------------------------------------------------------- cut-outs  (VERIFY!)
# All in local board frame.  X is board width (USB edge at -X), Y is up the incline.
USB_Y       = 0.0      # board USB-C centre along the -X short edge (est.)
# OPEN SIDE PORT: a normal USB-C cable plugs straight into the board through a slot in
# the -X wall.  (The rear panel-mount jack + its internal right-angle male are gone.)
USB_SLOT_Y  = 18.0     # side-port slot width  (along Y)
USB_SLOT_Z  = 12.0     # side-port slot height (Z: PCB + connector + plug body)
USB_SKIN    = 0.0      # 0 = the -X wall is cut through (open side port).  Set >0 to
                       # retain that much outer skin and make the pocket blind again.

RESET_X     = -38.0    # RESET tact button, back face, near the -X edge (est.)
RESET_Y     = 14.0718  # 7.0 mm centre-to-centre from the (-39, +21) corner hole
RESET_PIN_D = 3.0      # back-face pin hole Ø (paper-clip / SIM pin)

# microSD: NO case access feature.  The socket's card mouth is flush with the board's
# -Y (bottom) long edge, so the card enters up-incline from underneath the PCB -- it
# cannot be reached from the rear, and the face's bottom lip blocks it anyway.  The
# card is swapped with the board out of the case, so the back wall stays solid.
# (SD_X / SD_Y / SD_WIN_X / SD_WIN_Z removed -- there is no window to size.)

# The rear panel-mount USB-C jack is REMOVED (PANEL_* params deleted).  Power now enters
# through the open -X side port above, straight into the board's own USB-C.  A right-angle
# USB-C cable keeps the stand tight to the wall; a straight plug sticks out ~18 mm.

# microphone port: MEMS mic ports through the FRONT (bare PCB strip near the +X short
# edge, opposite the connectors) -> small hole through the BEZEL.
# Viewed from the front with the USB edge on the LEFT, +X is right and +Y is up, so the
# mic sits low on the right-hand bare strip.  MIC_Y is MEASURED: 8.0 mm up from the PCB's
# bottom (-Y) edge.  MIC_X is still a photo estimate.
# If the mic turns out rear-ported, move this hole to the shell instead.
MIC_X       = 40.0
MIC_Y       = -17.0    # = -PCB_H/2 + 8.0  -> 8 mm from the board's bottom edge
MIC_HOLE_D  = 2.0
MIC_CSK_D   = 4.5      # funnel Ø on the visible bezel face (tapers to MIC_HOLE_D)

# included kit speaker: enclosed 38 x 26 x 8 box, 8Ω / ~1 W (FM8002E amp), DOWNWARD-firing.
# Housed in the solid back of the wedge, loaded from the rear, firing down through a
# grille; feet lift the base for the air gap.  Footprint MEASURED (38 x 26); the box is
# laid 38-across / 26-deep because a 38 mm insertion depth would need BACK_Y ~= 57.
# It fires straight down, so the 90deg rotation costs nothing acoustically.
SPK_W        = 38.0    # box footprint X  (across the case)
SPK_L        = 26.0    # box footprint Y  (rear insertion depth)
SPK_T        = 8.0     # box thickness (Z)
SPK_SLOT_H   = 10.0    # the slot the box slides into is 10.0 high (2.0 over the 8 mm box)
SPK_FIT      = 0.6     # pocket clearance (X/Y only; Z is set by SPK_SLOT_H)
SPK_CX       = 0.0     # pocket centre X (clear of the -X cable channel)
GRILLE_T     = 1.5     # perforated floor thickness under the speaker
GRILLE_HOLE  = 2.0     # grille hole Ø
GRILLE_PITCH = 3.6     # grille hole spacing
SPK_WIRE_D   = 8.0     # channel from the pocket up to the board cavity (speaker lead)
FOOT         = 9.0     # foot pad size
FOOT_H       = 4.0     # base lift for the downward-firing air gap

# rear speaker load port + snap-in cap (separate part: speaker_cap.stl).  The speaker
# sits at the FRONT of the pocket; the cap fills a rear zone, backs the speaker in, and
# snaps into two catch recesses.  Snap fits often need per-printer tuning of CAP_CLR.
SPK_CAP_ZONE = 5.0     # rear space behind the speaker for the cap arms/catches
CAP_T        = 2.0     # cap face-plate thickness
CAP_OVER     = 1.8     # plate overlap beyond the port on each side
CAP_CLR      = 0.4     # snap/plug fit clearance
CAP_ARM_X    = 2.4     # snap-arm thickness
CAP_ARM_Z    = 8.0     # snap-arm height (within the port)
CAP_HOOK     = 1.6     # hook outward protrusion into the catch recess
CAP_CATCH_DEPTH = 2.0  # catch recess depth into the side wall
CAP_HOOK_RAMP = 1.5    # lead-out ramp height on the retaining face (bigger = easier pry)
CAP_PRY_W    = 12.0    # fingernail pull-tab width on the cap's bottom edge
CAP_PRY_LEN  = 3.0     # pull-tab reach past the plate (grab point)

SEG         = 96       # cylinder smoothness
