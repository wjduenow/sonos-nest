"""Single source of truth for the sonos-button-v3 shell + lid.

Same product as hardware/button-v2, on the Waveshare ESP32-S3-LCD-1.47B, which adds a 1.47"
ST7789 that is dark by default and wakes to show a QR code. So the box gains a screen window on
its FRONT wall; the FLM12-FJ-6 stays on TOP exactly as before.

Everything downstream is DERIVED from here. Constants that "happen to agree" are the ones that
rot, so if a number can be computed it is computed, with a comment saying what it prevents.

⚠️ marks any number not verified against a caliper or a vendor drawing.

--------------------------------------------------------------------------------------------
DATUM  (same convention as button-v2, so the two files read alike)
    z = 0        the OUTER TOP FACE — the plane the button's flange sits on
    +z           DOWNWARD into the box
    z = HEIGHT   the bottom face
    y = 0        the OUTER FRONT FACE — the wall carrying the screen window
    +y           BACKWARD into the box;  y = OUT_Y is the rear face, which is the LID
    x = 0        the width centreline

ASSEMBLY: the PCB screws to four M2 bosses standing off the inside of the FRONT wall, display
facing forward through the window. The REAR is the lid. Chosen over button-v2's ledge+rib scheme
because a screen has to stay square in its window, and ledges do not control rotation.
--------------------------------------------------------------------------------------------
"""


class MEASURE(float):
    """A placeholder for a number nobody has measured yet.

    Deliberately NOT a plausible default. A wrong-but-reasonable number is the failure mode this
    whole file exists to prevent: it prints, it builds, it exports an STL, and the error only
    surfaces as a part that does not fit. This raises the moment anything does arithmetic with it,
    so an unmeasured dimension cannot reach a mesh.
    """
    def __new__(cls, what):
        o = super().__new__(cls, float('nan'))
        o.what = what
        return o

    def _boom(self, *_a, **_k):
        raise ValueError(f"UNMEASURED: {self.what} — see hardware/button-v3/README.md §2")

    __add__ = __radd__ = __sub__ = __rsub__ = _boom
    __mul__ = __rmul__ = __truediv__ = __rtruediv__ = _boom
    __neg__ = __gt__ = __lt__ = __ge__ = __le__ = _boom


# ============================================================================================
# 1. THE BOARD — Waveshare ESP32-S3-LCD-1.47B
# ============================================================================================
# From Waveshare's dimension drawing (wiki image ESP32-S3-LCD-1.47B-details-size.jpg), read at
# 3x magnification. Waveshare publishes NO STEP model for this board — checked; the only Drive
# link on the wiki is the 363 MB Arduino installer.
PCB_W        = 36.37   # X — the long axis, horizontal in this build
PCB_H        = 20.32   # Z — the short axis, vertical
# ⚠️ NOT MEASURABLE ON THIS BOARD, AND IT DOES NOT MATTER MUCH — read the next block first.
# The LCD is seated directly on the PCB, so calipers cannot reach bare laminate. 1.6 is the
# overwhelming standard and the design is made INSENSITIVE to it: see GLASS_AIR below, which is
# sized to swallow +/-0.3 of error here without the glass ever touching the window rim.
PCB_T        = 1.6     # ⚠️ ASSUMED, not measured
PCB_CORNER   = 2.0     # ⚠️ eyeballed off the drawing. Cosmetic: only softens the board pocket.

# Header geometry, for keep-outs around the soldered harness.
HDR_PITCH    = 2.54    # drawing
HDR_ROW_GAP  = 17.78   # drawing — row centreline to row centreline.
# The one number on that drawing that looked like a hole pitch and is not. 7 x 2.54 = 17.78, and
# 17.78 + 2 x 1.27 = 20.32 EXACTLY, i.e. two pad rows inset 1.27 mm from each long edge. Reading
# it as a hole pitch would have put the bottom holes 0.98 mm off the end of the board.
HDR_EDGE_OFF = (PCB_H - HDR_ROW_GAP) / 2.0                     # = 1.27, DERIVED — never type it
HDR_LAST_TO_EDGE = 11.31   # drawing — last pad (TXD) centre to the right board edge

# --- USB-C ------------------------------------------------------------------------------------
# On a SHORT edge (the 20.32 one), so it exits the LEFT or RIGHT wall of this landscape box.
# CONFIRMED 2026-09-09: the USB-C is on the RIGHT short edge with the screen upright, so the
# cable exits the right-hand wall and the chin above is on that same side.
USB_ON_RIGHT = True
USB_PROTRUDE = 2.0     # MEASURED 2026-09-08 — shell overhang past the board edge
USB_WIDTH    = 9.0     # MEASURED 2026-09-09 — along the board's short axis
USB_HEIGHT   = 3.3     # MEASURED 2026-09-09 — standing proud of the component-side face.
# Third cross-check on the stack, and it closes: COMP_Z_MAX came out at 3.00 by subtraction
# (STACK_TOTAL - DISP_STACK) without anyone knowing what the tallest part was. Measuring the
# USB-C shell independently gives 3.3. So the connector IS the tallest thing on that face, as
# assumed, and the two routes agree to 0.3 mm. Irrelevant to the box in any case — the M12 nut
# leaves 9.48 mm of slack in this axis.
USB_OFF_Z    = PCB_H / 2.0   # CONFIRMED 2026-09-09 centred on that edge = 10.16, DERIVED

# --- Component stack -------------------------------------------------------------------------
# The display is on one face; everything else (ESP32-S3 module, USB-C, microSD socket) is on the
# other. Both numbers below are measured from the GLASS TOP, because that is the only datum a
# caliper can actually reach on an assembled board.
#
# The naming matters and cost one round trip. Asking for "display glass above the PCB's
# display-side face" is unanswerable here — the LCD covers that face. What comes back is glass top
# to PCB BACK face, i.e. the LCD module AND the PCB together. Two readings were taken (5.75, then
# 5.5) and read as two different quantities in conflict; they were the same quantity twice.
DISP_STACK   = 5.5     # MEASURED 2026-09-08 — glass top -> PCB BACK face (LCD module + PCB)
STACK_TOTAL  = 8.5     # MEASURED 2026-09-08 — glass top -> tallest part on the component side

# Falls straight out, and the fact that it lands on ~3.0 is the cross-check that the two
# measurements above were read correctly: a standard top-mount USB-C shell stands 3.16 mm proud,
# and it is the tallest thing on that face. Had DISP_STACK meant "LCD module alone", this would
# have come out at 1.4 — impossible for that connector, which is what flagged the misreading.
# Take the WORSE of the two routes. Subtraction says 3.00; the connector itself measures 3.3, and
# a cavity sized off the smaller number would foul it by 0.3.
COMP_Z_MAX   = max(STACK_TOTAL - DISP_STACK, USB_HEIGHT)       # = 3.30
LCD_MODULE_T = DISP_STACK - PCB_T                              # = 3.90, carries PCB_T's error

# --- The display window ----------------------------------------------------------------------
# The LIT area, not the glass. ~17.4 x 32.4 mm is what 172 x 320 px at this panel's pitch works
# out to, but the offsets are what actually matter and cannot be derived at all.
DISP_ACT_W   = 32.4    # MEASURED 2026-09-08 — matches 172 x 320 px at this panel's pitch exactly,
DISP_ACT_H   = 17.4    # which is a good sign the right thing was measured (the LIT area, not glass)
# ⚠️ THE LIT AREA IS NOT CENTRED ON THE PANEL — there is a ~3.5 mm DEAD CHIN on the USB-C side.
# Observed on hardware 2026-09-09 against the bring-up's 1-px border: three sides sit hard against
# the display edge, the USB-C side has 3.5 mm of dead glass. That is the driver IC bonded to one
# short edge of the native 172x320 panel, which rotation 1 puts on the right — ordinary panel
# construction, NOT a wrong column offset (that was checked: Arduino_TFT::setRotation maps
# ROW_OFFSET1 -> x and COL_OFFSET2 -> y at rotation 1, both correct here).
#
# It matters more than it sounds. Centre the window on the PCB and it crops ~1.75 mm of PIXELS on
# one side while showing ~1.75 mm of dead chin on the other — and cropped pixels are a reprint.
DISP_CHIN    = 3.5     # dead glass between the lit area and the display edge, USB-C side
# MEASURED 2026-09-09, and PCB-relative because the LCD module and the PCB share the SAME
# footprint (36.37 x 20.32) — the board is entirely behind the glass with no overhang either way,
# so "from the glass edge" and "from the PCB edge" are the same measurement here.
DISP_OFF_X   = 0.2     # PCB left edge (USB-C on the RIGHT) -> lit area left edge
DISP_OFF_Z   = 0.2     # PCB top edge -> lit area top edge
# Cross-check that these are read right: the derived right margin lands on the independently
# measured chin. 36.37 - 0.2 - 32.4 = 3.77 vs 3.5 measured — 0.27 apart, which is measurement
# noise. Two numbers taken different ways agreeing is what makes this trustworthy.
DISP_MARGIN_R = PCB_W - DISP_OFF_X - DISP_ACT_W                # = 3.77  (the chin)
DISP_MARGIN_B = PCB_H - DISP_OFF_Z - DISP_ACT_H                # = 2.72

# ⚠️ THE LIT AREA IS 0.2 mm FROM THE TOP AND LEFT BOARD EDGES, WHICH INVERTS THE MOUNTING.
#
# The LCD covers the whole PCB face, so nothing can touch the board's front surface at the
# corners: bosses rising off the front wall — the scheme originally chosen — cannot exist. And
# with only 0.2 mm of bezel on two edges there is no room for a retaining lip either; a printable
# one (>=0.8, one nozzle) would cover ~8 px of live display.
#
# Threaded eyelets resolve both at once. Screws come from the REAR and thread directly into the
# board, so the front wall never has to hold anything and the window can clear every pixel.
#
# It does mean nothing shell-integral may sit behind the board — the board is inserted from the
# rear, so anything already there would block it. The pillars therefore belong to the LID, and the
# lid is a deep tray whose floor sits behind the components carrying four short pillars. That also
# keeps the screws short: a pillar hung off a flat lid would need M2x18.
#
WINDOW_CLR   = 0.6     # window is the lit area + this per side. Generous on purpose: a window
                       # that crops the display is unfixable without a reprint, whereas a slightly
                       # loose one only shows a sliver of black bezel.
# ...but CLAMPED to the board outline, because on the top and left there is only 0.2 mm of bezel to
# spend and the clearance would otherwise run the opening PAST the board edge, leaving a hairline
# gap into the case. Flush with the board edge on those two sides is the right answer: no overhang
# means no chance of the wall creeping over live pixels, and the board's own edge closes the seam.
WINDOW_X0    = max(DISP_OFF_X - WINDOW_CLR, 0.0)               # = 0.00  (flush)
WINDOW_Z0    = max(DISP_OFF_Z - WINDOW_CLR, 0.0)               # = 0.00  (flush)
WINDOW_X1    = min(DISP_OFF_X + DISP_ACT_W + WINDOW_CLR, PCB_W)   # = 33.20
WINDOW_Z1    = min(DISP_OFF_Z + DISP_ACT_H + WINDOW_CLR, PCB_H)   # = 18.20

# --- Mounting holes ---------------------------------------------------------------------------
# M2, four, brass eyelets, one per corner — the "M2" callout on the drawing is explicit.
#
# ⚠️⚠️ THE FOUR CENTRES ARE UNMEASURED, AND THE DRAWING SUGGESTS THEY ARE NOT SYMMETRIC.
# It gives the top-LEFT hole as 1.97 from the left edge / 3.52 from the top edge, and the
# top-RIGHT as 2.40 from the right edge / **2.00** from the top edge. A 1.5 mm difference in the
# Y inset between two corners of the same board is unusual enough that it is either real — in
# which case guessing symmetry puts two bosses 1.5 mm out — or an artefact of reading leaders off
# a JPEG. Either way it is not something to bet a print on. Caliper all four.
# CONFIRMED 2026-09-09: all four eyelets stay CLEAR of the LCD module on the display face, so
# bosses rising off the front wall can reach the PCB's front face at each corner. That is what
# makes the front-wall boss scheme viable at all — had the module overlapped them, the board would
# have had to be carried from the rear and the screen's depth in the window would have been set by
# a tolerance stack instead of by one surface.
HOLES_CLEAR_OF_LCD = True
HOLE_D       = 2.0     # M2 nominal
HOLE_CLR_D   = 2.6     # clearance hole in the PCB pocket / boss pilot spacing. 0.3 mm of radial
                       # slop per hole, deliberately, so a +/-0.3 error in any measured centre
                       # still assembles rather than binding across four bosses.
# MEASURED 2026-09-09: z = 3.5 and 16.8 from the top edge. 3.5 + 16.8 = 20.3 against a 20.32
# board, so the holes ARE symmetric about the long centreline — 3.5 in from each long edge. That
# also settles the drawing: its top-left 3.52 was right and its top-right "2.00" was a misread
# leader, so the asymmetry flagged earlier is not real. Vertical pitch 13.3.
HOLE_Z1      = 3.5
HOLE_Z2      = 16.8
HOLE_Z_PITCH = HOLE_Z2 - HOLE_Z1                               # = 13.30, DERIVED

# ⚠️ X IS STILL OPEN. The drawing gives insets of 1.97 (left) and 2.40 (right) -> x = 1.97 and
# 33.97; the first caliper pass reported "1 and 33". Both give a 32.00 pitch, so the SPACING is
# solid — it is the absolute position that differs by ~1 mm, which the 2.6 clearance hole (0.3 of
# slop) does NOT absorb.
HOLE_X_PITCH = 32.0    # agreed by both sources
# RESOLVED 2026-09-09 in favour of the drawing. The eyelets are THREADED M2 inserts, whose brass
# body measures ~4 mm across on the vendor drawing — so a centre 1.0 mm from the board edge would
# put a millimetre of that body off the PCB. Impossible. At 1.97 it lands tangent to the edge,
# which is exactly how it looks in Waveshare's photo. The caliper pass read "1 and 33", each
# exactly one hole-radius short of 1.97 and 33.97: the signature of measuring to the NEAR EDGE of
# the hole rather than its centre, easy to do on an eyelet where the centre is not a visible
# feature. (The z pass did not have the problem — 3.5 + 16.8 = 20.3 proves those are centres.)
HOLE_X1      = 1.97
HOLE_X2      = HOLE_X1 + HOLE_X_PITCH                          # = 33.97, DERIVED
EYELET_THREADED = True   # CONFIRMED 2026-09-09 — an M2 screw bites

# ============================================================================================
# 2. THE BUTTON — FILN FLM12-FJ-6, identical to button-v2 and cam-button
# ============================================================================================
BUTTON_BODY_D    = 11.71   # measured thread major diameter (plans/04 §6)
BUTTON_BORE_D    = 12.0    # thread + ~0.3. FDM prints holes undersize — test-coupon it.
BUTTON_HEAD_D    = 14.0    # datasheet ø14 flange; sits ON the outer face and hides the bore
BUTTON_NUT_AF    = 16.0    # datasheet hex ACROSS FLATS
BUTTON_NUT_AC    = BUTTON_NUT_AF / 0.8660254   # = 18.48 ACROSS CORNERS — the real keep-out, and
                                               # the number that sets this box's DEPTH. Deriving
                                               # it stops anyone "simplifying" it back to 16.
BUTTON_CLR       = 0.5
BUTTON_OVERALL_T = 14.0    # MEASURED 2026-08-19 — dome top to back of connector. Supersedes both
                           # readings of the datasheet; see CLAUDE.md.
BUTTON_HEAD_T    = 1.5
BUTTON_INSIDE_T  = BUTTON_OVERALL_T - BUTTON_HEAD_T            # = 12.50, DERIVED — never type it
BUTTON_THREAD_L  = 4.0
BUTTON_NUT_T     = 2.0     # ⚠️ ESTIMATED — an M12x0.75 thin nut. Caliper it with the button.
BUTTON_PANEL_T   = BUTTON_THREAD_L - BUTTON_NUT_T              # = 2.00, DERIVED — the wall
                                                               # thickness the button can clamp
BUTTON_TAIL_T    = 2.5     # the connector/solder tail behind the body
BUTTON_CX        = 0.0     # centred on width. The top face has room for nothing else anyway.

# ============================================================================================
# 3. THE SHELL
# ============================================================================================
WALL         = 2.5     # side, top and bottom walls
FRONT_WALL   = 2.5     # the wall carrying the screen window
LID_T        = 3.0     # the REAR face (the lid)
PCB_X_GAP    = 0.4     # cavity is PCB + this per side in X
PCB_Z_GAP    = 0.4     # ...and in Z

# --- Depth (Y) — set by the NUT, not by the PCB ------------------------------------------------
# This is the one dimension where the electronics are not in charge. The M12 nut tightens from
# INSIDE, so the cavity has to swallow 18.48 mm across corners plus a driver's worth of slack —
# comfortably more than the display + PCB + components stack needs. button-v2 has the identical
# max() and for the identical reason.
NUT_SOCKET_CLR = 0.5
# Front wall inner face -> display glass. 1.0, not the 0.3 that would merely clear it, because
# this gap is ALSO the error budget for PCB_T being assumed rather than measured: the bosses set
# the PCB's front face, so a PCB 0.3 thicker than assumed pushes the glass 0.3 closer to the wall.
# At 1.0 that is still 0.7 of air. The window is chamfered so the recess reads as deliberate.
GLASS_AIR      = 1.0
STACK_REAR_CLR = 0.5     # tallest component -> the lid
BOSS_STANDOFF  = GLASS_AIR + LCD_MODULE_T                      # = 4.90 — front wall inner face to
                                                               # the PCB's display-side face
_STACK_Y = GLASS_AIR + STACK_TOTAL + STACK_REAR_CLR            # = 9.30, the whole electronics stack

# And this is the number that decides the box. 19.48 vs 9.30: the M12 nut needs MORE THAN TWICE
# the depth the entire display-plus-PCB-plus-components stack does. Anyone looking at this box
# will assume the screen made it deep; it did not, the button did. Do not "reclaim" this depth by
# trimming clearances around the board — it is not the board's.
IN_Y   = max(BUTTON_NUT_AC + 2 * NUT_SOCKET_CLR, _STACK_Y)     # = 19.48
OUT_Y  = IN_Y + FRONT_WALL + LID_T                             # = 24.98

# --- Width (X) --------------------------------------------------------------------------------
IN_X   = PCB_W + 2 * PCB_X_GAP                                 # = 37.17
OUT_X  = IN_X + 2 * WALL                                       # = 42.17

# --- Height (Z) — set by the BUTTON sitting above the board ------------------------------------
# The button's body plus its solder tail must clear the board's top edge entirely; the board
# cannot tuck under it because it spans the full width. That stack IS the height budget.
BUTTON_KEEPOUT_Z = BUTTON_INSIDE_T + BUTTON_TAIL_T             # = 15.00
BUTTON_PCB_CLR   = 1.0     # air between the button's tail and the board's top edge
PCB_TOP_Z    = BUTTON_KEEPOUT_Z + BUTTON_PCB_CLR               # = 16.00
PCB_BOT_Z    = PCB_TOP_Z + PCB_H                               # = 36.32
HEIGHT       = PCB_BOT_Z + PCB_Z_GAP + WALL                    # = 39.22  OVERALL

# ============================================================================================
# 4. WHERE THE BOARD ACTUALLY SITS  (world coordinates — see the datum at the top)
# ============================================================================================
# Board X: the cavity walls locate it. IN_X is PCB_W + 0.4 per side, so nothing else has to.
BOARD_X0 = -PCB_W / 2.0
BOARD_X1 = +PCB_W / 2.0                 # the USB-C edge — "right" as you face the screen

# Board Y: one chain, front to back, every link measured.
BOARD_Y_GLASS     = FRONT_WALL + GLASS_AIR                     # = 3.50  display glass
BOARD_Y_PCB_FRONT = BOARD_Y_GLASS + LCD_MODULE_T               # = 7.40
BOARD_Y_PCB_BACK  = BOARD_Y_PCB_FRONT + PCB_T                  # = 9.00  the pillars land here
BOARD_Y_REAR      = BOARD_Y_PCB_BACK + COMP_Z_MAX              # = 12.30 tallest component

# Window, in world coords.
WIN_X0 = BOARD_X0 + WINDOW_X0
WIN_X1 = BOARD_X0 + WINDOW_X1
WIN_Z0 = PCB_TOP_Z + WINDOW_Z0
WIN_Z1 = PCB_TOP_Z + WINDOW_Z1

# The four threaded eyelets, in world coords.
HOLE_XS = (BOARD_X0 + HOLE_X1, BOARD_X0 + HOLE_X2)             # = -16.215, +15.815
HOLE_ZS = (PCB_TOP_Z + HOLE_Z1, PCB_TOP_Z + HOLE_Z2)           # = 19.50, 32.80

# ============================================================================================
# 5. RETENTION — four pillars on the LID, screws from outside it into the eyelets
# ============================================================================================
PILLAR_OD    = 4.5     # ⚠️ NOT arbitrary: at x = +/-16.2 a 5.0 pillar would foul the cavity wall
                       # (IN_X/2 = 18.585). check_clearances() asserts it.
PILLAR_BORE  = 2.6     # M2 clearance. 0.3 radial slop, which is the tolerance budget for print
                       # shrink and for HOLE_X1 coming off a drawing rather than a caliper.
PILLAR_LEN   = (OUT_Y - LID_T) - BOARD_Y_PCB_BACK              # = 12.98
SCREW_ENGAGE = 2.5     # thread engagement into the brass eyelet
SCREW_LEN    = LID_T + PILLAR_LEN + SCREW_ENGAGE               # = 18.48 -> M2 x 18
# ⚠️ M2 x 18 is a long screw for a box this size, and that is the price of the board loading from
# the rear: nothing shell-integral may stand behind it, so the pillars have to reach all the way
# from the lid. A stepped lid tray would shorten them to ~M2 x 8 at the cost of a 12 mm skirt and
# a stepped floor to clear the M12 nut. Not worth it for four screws that carry no load.

BOARD_STOP_Z = 1.2     # rib across the top of the board pocket, so the board cannot ride up into
                       # the button during assembly. Sits above WIN_Z0, so it never sees the window.

# --- Lid retention (the lid holds the board, so something must hold the lid) ---
LID_POST_OD    = 6.0
LID_POST_PILOT = 2.5   # M3 self-tap into the post
LID_SCREW_D    = 3.0
SPIGOT_T       = 1.5   # tongue depth into the cavity mouth
SPIGOT_CLR     = 0.25  # per side
SPIGOT_W       = 2.0   # ring width — a solid plate fouls the lid-screw posts
# ⚠️ Position is FORCED, not chosen. The board fills the cavity across almost its full width
# (36.37 in a 37.17 opening) so nothing fits beside it; there is 0.4 mm below it; above it is the
# button. The M12 nut's swept circle is 19.48 across on x = 0, leaving exactly two slivers at
# |x| > 9.74 up in the button region. The posts go there, and there is nowhere else.
LID_POST_X   = 14.0
LID_POST_Z   = 8.0
LID_POST_Y0  = FRONT_WALL + 1.0    # they start just behind the front wall and run to the lid

# --- USB-C notch, right-hand wall -------------------------------------------------------------
# Generous on purpose. Unlike the screen window, nothing here needs precision: it clears a cable
# overmold, which is bigger and less well specified than the receptacle.
USB_SLOT_W  = USB_WIDTH + 3.0                                  # = 12.00, in Z
USB_SLOT_Y0 = BOARD_Y_PCB_BACK - 1.0                           # = 8.00
USB_SLOT_Y1 = BOARD_Y_REAR + 2.5                               # = 14.80
USB_SLOT_ZC = PCB_TOP_Z + USB_OFF_Z                            # = 26.16, the receptacle centreline

OUT_R = 3.0            # outer vertical corner radius


# The shell body stops short of the rear; the lid makes up OUT_Y.
OUT_Y_SHELL  = OUT_Y - LID_T                                   # = 21.98
BUTTON_Y     = FRONT_WALL + IN_Y / 2.0                         # = 12.24, centred in the cavity
NUT_RELIEF_D = BUTTON_NUT_AC + 1.0                             # = 19.48, the nut's swept circle

SEG = 96      # cylinder smoothness


if __name__ == "__main__":
    print(f"  PCB            {PCB_W} x {PCB_H} mm, {PCB_T} thick (assumed)")
    print(f"  stack          LCD module {LCD_MODULE_T:.2f} + PCB {PCB_T} + components {COMP_Z_MAX:.2f}"
          f"  = {STACK_TOTAL} measured")
    print(f"  boss standoff  {BOSS_STANDOFF:.2f} from the front wall inner face")
    print(f"  header rows    {HDR_ROW_GAP} apart, {HDR_EDGE_OFF:.2f} from each long edge (derived)")
    print(f"  button keepout {BUTTON_KEEPOUT_Z:.2f} deep, {BUTTON_NUT_AC:.2f} across corners")
    print(f"  PCB top edge   z = {PCB_TOP_Z:.2f}")
    print(f"  electronics    {_STACK_Y:.2f} deep;  M12 nut {BUTTON_NUT_AC + 2 * NUT_SOCKET_CLR:.2f}"
          f"  -> the NUT sets the depth, by {BUTTON_NUT_AC + 2 * NUT_SOCKET_CLR - _STACK_Y:.2f} mm")
    print(f"  OVERALL        {OUT_X:.2f} x {OUT_Y:.2f} x {HEIGHT:.2f} mm"
          f"   ({OUT_X * OUT_Y * HEIGHT / 1000.0:.1f} cm3 bounding)")
