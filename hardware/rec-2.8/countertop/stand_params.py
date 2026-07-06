"""
Shared geometry for the Hosyond / LCDWIKI ES3C28P 2.8" ESP32-S3 display
nightstand STAND CASE.  build_shell.py and build_bezel.py both import this so the
two parts can never drift apart.

Board is the ES3C28P (SKU on the QDtech "LCM OUTLINE" drawing, V1.0 2025-06-11).
All board numbers below are taken straight from that drawing / the LCDWIKI wiki:

    PCB            86.0 (H) x 50.0 (W) x 1.6 mm, corner radius R3.5
    mount holes    4x Ø3.2, on a 42 x 78 mm rectangle  -> 4.0 mm in from each edge
                   (Ø5.6 keep-out ring around each: screw heads <= ~5.6 are safe)
    front glass    protrudes 4.30 mm above the PCB; glass 50.0 x 69.2 (full width)
    active area    43.20 x 57.60 ; viewing area 43.60 x 58.05
    back parts     up to 4.70 mm tall ; total module thickness 10.60 mm
    connectors     USB-C + RESET + BOOT on ONE short (50 mm) edge
    RESET / BOOT   tact buttons on the BACK face near that edge
    microSD        push-push socket MID-BOARD on the back (NOT on a perimeter edge)

MOUNTING: LANDSCAPE (86 mm wide, 50 mm tall), reclined 20deg from vertical.
Screen faces up-and-out for reading on a nightstand.

    +X  board width  (86 mm, horizontal)   -> the USB-C short edge is at -X
    +Y  depth        (toward the back of the stand)
    +Z  height       (up from the nightstand surface)

The board sits in a pocket on the reclined front face.  Local board-frame Z is
the screen normal: z=0 is the PCB front plane (== the face surface), +z points out
toward the viewer (glass lives here), -z goes back into the body (components here).

  !!! POSITIONS TO VERIFY WITH CALIPERS ON A REAL BOARD !!!
  The USB-C / RESET / microSD in-plane positions (USB_Y, RESET_XY, SD_XY) are best
  estimates from the board photos, NOT dimensioned on the outline drawing. The
  cut-outs are drawn generously and are single-line parameters here -- measure the
  actual board and nudge these before the final print.  render_preview.py plots
  them on a board map so you can eyeball the alignment.
"""

# ---------------------------------------------------------------- board (verified)
PCB_W       = 86.0     # X, landscape width
PCB_H       = 50.0     # Y, landscape height (up the incline)
PCB_T       = 1.6
PCB_CORNER  = 3.5      # R3.5 board corner radius
HOLE_D      = 3.2      # Ø3.2 mounting holes
HOLE_DX     = 78.0     # hole spacing along X (the 86 mm axis)
HOLE_DY     = 42.0     # hole spacing along Y (the 50 mm axis)
HOLE_KEEPOUT = 5.6     # Ø5.6 no-component ring -> boss OD stays under this
COMP_H      = 4.70     # tallest back-side component
GLASS_PROUD = 4.30     # glass stack height above the PCB front face
GLASS_W     = 69.2     # glass along X (landscape); flush to board in Y
GLASS_H     = 50.0
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
BACK_Y      = 40.0     # back wall depth (base footprint 0..BACK_Y)
WALL        = 3.0      # nominal wall thickness target

# pocket that the board drops into (board footprint + clearance)
FIT         = 0.4      # per-side clearance around the PCB in the pocket
BACK_CLR    = 2.0      # gap behind the tallest component to the pocket floor
FACE_LEN    = PCB_H + MB + MT           # 66 mm reclined front face length

# board-mount bosses (self-tapping) at the 4 corner holes
BOSS_OD     = 5.4                        # < Ø5.6 keep-out
BOSS_PILOT  = 2.6                        # pilot for an M3 self-tapping screw
BOARD_SCREW_HEAD = 5.4                   # keep heads within the keep-out ring

# bezel (screwed-on front cover) + the raised rim it sits flush on
BEZEL_T     = 3.0                        # bezel frame thickness
BEZEL_OUT_X = W_OUT / 2                  # bezel outer == shell face outer -> flush
BEZEL_OUT_Y = FACE_LEN / 2
BEZEL_R     = 4.0                        # bezel + rim outer corner radius
RIM_H       = GLASS_PROUD                # raised rim height (== glass proud) -> flush cap
OPEN_MARGIN = 0.7                        # screen opening = VA + this per side
POST_X      = 39.0                       # bezel screws land in the top/bottom rim band
POST_Y      = 30.0
POST_PILOT  = 2.6                        # M2.5/M3 self-tapping bezel screws
BEZEL_SCREW_HEAD = 5.0                   # countersink head Ø

# ---------------------------------------------------------------- cut-outs  (VERIFY!)
# All in local board frame.  X is board width (USB edge at -X), Y is up the incline.
USB_Y       = 0.0      # USB-C centre along the -X short edge (est.)
USB_SLOT_Y  = 13.0     # cut-out width  (along Y) -- generous for cable overmold
USB_SLOT_Z  = 7.0      # cut-out height (across the PCB thickness + connector)

RESET_X     = -38.0    # RESET tact button, back face, near the -X edge (est.)
RESET_Y     = 15.0
RESET_PIN_D = 3.0      # back-face pin hole Ø (paper-clip / SIM pin)

SD_X        = 6.0      # microSD socket centre, mid-board on the back (est.)
SD_Y        = 9.0
SD_WIN_X    = 15.0     # back access window size to reach/eject the card
SD_WIN_Z    = 12.0

# cable management: the USB-C plug enters the -X side, then the cable is routed
# DOWN the -X face and BACK under the base to exit at the rear.  Suits a right-angle
# USB-C cable best; a straight plug sticks out ~18 mm then drops into the down groove.
CABLE_W     = 7.0      # open channel width (fits a USB-C cable, not the plug)
CABLE_D     = 5.0      # channel depth into the wall / groove height

# snap-in cable clips: nubs that bulge in from each channel wall at the opening so a
# ~4 mm cable presses past them and is retained (gap = CABLE_W - 2*CLIP_NUB).
CLIP_NUB    = 1.75     # inward protrusion per side  -> 3.5 mm retained gap
CLIP_LEN    = 3.0      # clip length along the channel (Y)
CLIP_H      = 1.4      # clip height up from the base bottom
CLIP_YS     = (11.0, 24.0, 36.0)   # clip positions along the channel

# microphone port: MEMS mic ports through the FRONT (bare PCB strip near the +X short
# edge, opposite the connectors) -> small hole through the BEZEL.  Position est. from
# the board photos -- VERIFY.  If the mic turns out rear-ported, move this to the shell.
MIC_X       = 40.0
MIC_Y       = 12.0
MIC_HOLE_D  = 2.0
MIC_CSK_D   = 4.5      # funnel Ø on the visible bezel face (tapers to MIC_HOLE_D)

# included kit speaker: enclosed ~20x15 box, 8Ω / ~1 W (FM8002E amp), DOWNWARD-firing.
# Housed in the solid back of the wedge, loaded from the rear, firing down through a
# grille; feet lift the base for the air gap.  *** SIZE ESTIMATED -- VERIFY the box ***
SPK_W        = 20.0    # box footprint X
SPK_L        = 15.0    # box footprint Y (rear insertion depth)
SPK_T        = 11.0    # box thickness (Z)
SPK_FIT      = 0.6     # pocket clearance
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
