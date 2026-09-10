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
# ⚠️ 7.5, CORRECTED FROM 5.5 ON A REAL ASSEMBLY (2026-09-09). At 5.5 the LCD stood 2.0 mm proud of
# the bezel, which is this number and only this number: the posts put the PCB's back face here, and
# the glass sits DISP_STACK in front of it, so an error here moves the glass one-for-one.
#
# Note the fix is NOT "shorten the posts by 2 mm", tempting as that sounds. The posts stay 3.80 —
# what moves is the whole board-and-plane group, because the middle plane rides 0.5 mm behind the
# components. Shortening the posts alone would pull the board back into a plane that stayed put and
# bury 1.5 mm of USB-C connector in it.
#
# ⚠️ AND IT LEAVES STACK_TOTAL INCONSISTENT — re-measure both on the new board. 7.5 + 3.3 = 10.8,
# against a measured total of 8.5. One of the two is wrong by ~2 mm and the assembly says it is not
# this one. COMP_Z_MAX takes the max of the two routes, so the cavity is still sized off the USB-C
# connector's own measured height and cannot come out short.
DISP_STACK   = 7.5     # glass top -> PCB BACK face (LCD module + PCB)
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
# ⚠️ 11.85, RE-MEASURED 2026-09-09 on the button in hand. plans/04 §6 recorded 11.71, and the
# 0.14 mm difference mattered: the bore was a typed 12.0, i.e. 0.15 of nominal clearance, and FDM
# prints holes UNDERSIZE — so the printed hole came out at or below the thread and the button
# would not pass. Reported from a real print as "a little tight".
BUTTON_BODY_D    = 11.85   # thread major diameter
BUTTON_BORE_CLR  = 0.45    # spend it freely — see below
BUTTON_BORE_D    = BUTTON_BODY_D + BUTTON_BORE_CLR             # = 12.30, DERIVED

# Why 0.45 and not a tighter fit: the Ø14 flange sits ON the outer face and covers the bore
# completely, so slop here is INVISIBLE, while a bore 0.1 too small is a part you cannot assemble
# without a file. The asymmetry is total, so the clearance goes where the risk is. It is also
# derived now rather than typed, which is what let a 0.14 mm change in the thread silently eat the
# entire margin last time.
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

# ⚠️ THE BOARD IS CENTRED, NOT THE LIT AREA — AND THAT IS A REVERSAL, ON PURPOSE.
#
# It was the other way round while the bezel covered the glass down to the lit rectangle: what you
# saw was the picture, so the picture got centred. Now the bezel exposes the WHOLE glass, so the
# visible rectangle is the glass — and centring the lit area instead would leave that opening
# 1.79 mm off centre, with left and right frame margins differing by 3.57 mm. Obvious. The lit area
# now sits its natural 1.79 mm right of centre inside the glass, which is simply where this panel's
# driver chin puts it, exactly as it does on any phone.
#
# It also buys back the width the offset had cost: 42.17 instead of 45.74.
DISP_CX = DISP_OFF_X + DISP_ACT_W / 2.0        # = 16.40, lit centre in board coords (for checks)
DISP_CZ = DISP_OFF_Z + DISP_ACT_H / 2.0        # = 8.90

BOARD_X0 = -PCB_W / 2.0                         # = -18.185
BOARD_X1 = +PCB_W / 2.0                         # = +18.185, the USB-C edge

# ============================================================================================
# 3. THE SHELL
# ============================================================================================
WALL         = 2.5     # side, top and bottom walls of the body
BEZEL_T      = 3.0     # the FRONT frame. The glass sits flush in its opening, so this thickness is
                       # also how far the frame wraps around the LCD's edge.
BACK_T       = 2.5     # the rear cover
FRONT_WALL   = BEZEL_T # alias — several derivations below read this name

# ⚠️ THE GLASS IS FLUSH WITH THE OUTSIDE OF THE BEZEL — y = 0 IS THE GLASS, not a wall.
# The bezel opening is the FULL board outline, dead border and chin included, so nothing overlaps
# the display at all and the front reads as one continuous surface. Nothing holds the board
# forward either: the four M2 screws into the middle plane do all of it.
BEZEL_GAP    = 0.15    # around the board in the bezel's opening
PCB_X_GAP    = 0.4     # cavity is PCB + this per side in X
PCB_Z_GAP    = 0.4     # ...and in Z

# --- Depth (Y) — set by the NUT, not by the PCB ------------------------------------------------
# This is the one dimension where the electronics are not in charge. The M12 nut tightens from
# INSIDE, so the cavity has to swallow 18.48 mm across corners plus a driver's worth of slack —
# comfortably more than the display + PCB + components stack needs. button-v2 has the identical
# max() and for the identical reason.
NUT_SOCKET_CLR = 0.5
GLASS_AIR      = 0.0   # the glass IS the front surface — see BEZEL_GAP above
STACK_REAR_CLR = 0.5     # tallest component -> the lid
BOSS_STANDOFF  = GLASS_AIR + LCD_MODULE_T                      # = 4.90 — front wall inner face to
                                                               # the PCB's display-side face
_STACK_Y = GLASS_AIR + STACK_TOTAL + STACK_REAR_CLR            # = 9.30, the whole electronics stack

# And this is the number that decides the box. 19.48 vs 9.30: the M12 nut needs MORE THAN TWICE
# the depth the entire display-plus-PCB-plus-components stack does. Anyone looking at this box
# will assume the screen made it deep; it did not, the button did. Do not "reclaim" this depth by
# trimming clearances around the board — it is not the board's.
IN_Y   = max(BUTTON_NUT_AC + 2 * NUT_SOCKET_CLR, _STACK_Y)     # = 19.48
OUT_Y  = IN_Y + BEZEL_T + BACK_T                               # = 24.98

# The BODY spans everything behind the bezel, and its rear is closed — there is no rear lid.
BODY_Y0      = BEZEL_T                                         # = 3.00, where the body starts
BODY_Y1      = OUT_Y - BACK_T                                  # = 22.48, where the back cover starts
CAVITY_Y1    = BODY_Y1
BUTTON_Y     = FRONT_WALL + IN_Y / 2.0                         # = 12.24, centred in the cavity
NUT_RELIEF_D = BUTTON_NUT_AC + 1.0                             # = 19.48, the nut's swept circle

# --- Width (X) --------------------------------------------------------------------------------
# The cavity is symmetric about x = 0 but the board inside it is not, so its half-width is set by
# whichever board edge ended up further out — the USB-C side, after the offset above.
IN_X_HALF = PCB_W / 2.0 + PCB_X_GAP                            # = 18.585
IN_X   = 2 * IN_X_HALF                                         # = 37.17
OUT_X  = IN_X + 2 * WALL                                       # = 42.17

# --- Height (Z) — set by the BUTTON sitting above the board ------------------------------------
# The button's body plus its solder tail must clear the board's top edge entirely; the board
# cannot tuck under it because it spans the full width. That stack IS the height budget.
BUTTON_KEEPOUT_Z = BUTTON_INSIDE_T + BUTTON_TAIL_T             # = 15.00
BUTTON_PCB_CLR   = 1.0     # air between the button's tail and the board's top edge
PCB_TOP_Z    = BUTTON_KEEPOUT_Z + BUTTON_PCB_CLR               # = 16.00  — PINNED by the button
PCB_BOT_Z    = PCB_TOP_Z + PCB_H                               # = 36.32

# Height is set by centring the lit area, not by the board. The board's top edge cannot move — the
# button owns everything above z = 16 — so the box grows DOWNWARD, and that new space below the
# board is exactly where the second pair of lid posts goes. One change, two problems.
HEIGHT       = 2.0 * (PCB_TOP_Z + PCB_H / 2.0)                 # = 52.32  OVERALL
HEIGHT_MIN   = PCB_BOT_Z + PCB_Z_GAP + WALL                    # = 39.22, what the board alone needs

# ============================================================================================
# 4. WHERE THE BOARD ACTUALLY SITS  (world coordinates — see the datum at the top)
# ============================================================================================
# Board Y: one chain, front to back, every link measured.
BOARD_Y_GLASS     = 0.0                                        # flush with the outside
BOARD_Y_PCB_FRONT = BOARD_Y_GLASS + LCD_MODULE_T               # = 7.40
BOARD_Y_PCB_BACK  = BOARD_Y_PCB_FRONT + PCB_T                  # = 9.00  the pillars land here
BOARD_Y_REAR      = BOARD_Y_PCB_BACK + COMP_Z_MAX              # = 12.30 tallest component

# The bezel opening, in world coords: the FULL board outline plus a hairline. No lip, no overlap —
# the whole glass shows and sits flush in it.
WIN_X0 = BOARD_X0 - BEZEL_GAP
WIN_X1 = BOARD_X1 + BEZEL_GAP
WIN_Z0 = PCB_TOP_Z - BEZEL_GAP
WIN_Z1 = PCB_BOT_Z + BEZEL_GAP

# The four threaded eyelets, in world coords.
HOLE_XS = (BOARD_X0 + HOLE_X1, BOARD_X0 + HOLE_X2)             # = -16.215, +15.815
HOLE_ZS = (PCB_TOP_Z + HOLE_Z1, PCB_TOP_Z + HOLE_Z2)           # = 19.50, 32.80

# ============================================================================================
# 5. RETENTION — four pillars on the LID, screws from outside it into the eyelets
# ============================================================================================
# THE MIDDLE PLANE — the carrier plate, printed as part of the body.
#
# It is what the board screws to, from BEHIND, with the back cover off. That is the whole trick:
# the plane sits 0.5 mm behind the tallest component, so the screws are short (M2 x 8), and there
# is ~10.5 mm of open cavity behind it for a driver. The earlier designs each failed one of those
# two — long screws, or a joint you could not reach.
#
# The board still loads from the FRONT, through the bezel opening, and lands on the posts.
MID_Y0       = BOARD_Y_REAR + STACK_REAR_CLR                   # = 9.30, the plane's front face
MID_T        = 2.7
MID_Y1       = MID_Y0 + MID_T                                  # = 12.00
# ⚠️ 4.0, NOT 4.5 — set by the USB-C receptacle, not by the cavity wall. The lower-right eyelet is
# 2.16 mm from the edge of the 9 mm connector, so a 4.5 post (radius 2.25) buries 0.09 mm of itself
# in the connector body. Found by intersecting the body with a modelled receptacle; no dimensional
# check was looking at that pair, because the post and the connector are on different parts.
POST_OD      = 4.0
POST_BORE    = 2.6     # M2 clearance, 0.3 of radial slop

# ⚠️ THE PLANE'S TOP EDGE IS PINNED BY TWO THINGS AT ONCE, FROM OPPOSITE DIRECTIONS.
#
# It ran 2 mm above the board's top edge, to z = 14, and that was wrong twice over. The button's
# solder tail reaches z = 15 on the centreline, so the plane was sitting INSIDE the button — found
# by intersecting the body with a modelled button, not by any dimension. And the board's header
# pads sit at z = 17.27, with only 0.5 mm between the board's back face and the plane: the harness
# had nowhere to leave the pads.
#
# So it starts exactly where the upper posts need support and not a millimetre earlier. That clears
# the button by 2.5 mm and leaves a 1.5 mm band behind the board's top edge for the four harness
# wires to run along — which is the only route they have.
MID_Z0       = PCB_TOP_Z + HOLE_Z1 - POST_OD / 2.0             # = 17.50, the upper posts' base
MID_Z1       = PCB_BOT_Z + 2.0                                 # short of the lower corner bosses
HARNESS_BAND = MID_Z0 - PCB_TOP_Z                              # = 1.50, behind the board's top edge

POST_LEN     = MID_Y0 - BOARD_Y_PCB_BACK                       # = 3.80, over the components

# ⚠️ SCREW LENGTH IS PINNED FROM BOTH ENDS — derived and asserted, never picked. Short of 1.0 mm
# of engagement it does not hold; past PCB_T it drives through the board into the back of the LCD.
# A brass eyelet in a 1.6 mm board offers at most PCB_T of thread, so MID_T is tuned to put a stock
# M2 x 8 inside that window.
BOARD_SCREW_PASS   = MID_T + POST_LEN                          # = 6.50 to cross
BOARD_SCREW_LEN    = 8.0                                       # M2 x 8
BOARD_SCREW_ENGAGE = BOARD_SCREW_LEN - BOARD_SCREW_PASS        # = 1.50, must be <= PCB_T

BOARD_STOP_Z = 1.2     # rib across the top of the board pocket, so the board cannot ride up into
                       # the button during assembly. Sits above WIN_Z0, so it never sees the window.

# The spigot is no longer just a locator: it reaches all the way forward to bear on the carrier's
# back face, so the lid is what holds the whole stack against the front wall. Sized by subtraction
# rather than typed, or it silently stops touching the moment anything upstream moves.
# The bezel's four screws. Tucked into the CORNERS and deliberately overlapping the walls by
# BOSS_MERGE, so each boss is tied to two walls instead of standing alone — far better in pull-out,
# which matters here because these four screws are the only thing holding the board down.
BOSS_OD      = 7.0
BOSS_PILOT   = 2.5     # M3 self-tap
BOSS_MERGE   = 1.0     # how far the boss buries itself in each wall
BEZEL_SCREW_D = 3.0
BEZEL_SCREW_HEAD_D = 6.0   # countersunk M3
BEZEL_SCREW_LEN = 8.0      # M3 x 8: through BEZEL_T and ~5 into the boss
BACK_SCREW_LEN  = 8.0      # M3 x 8 the other way, into the same boss from the rear
BOSS_PILOT_D    = 6.0      # how deep each pilot goes in from its own end
BOSS_LEN     = 9.0     # boss depth, comfortably past the screw's ~5 of engagement
RIM_W        = 1.2     # bezel locating-rim wall

BOSS_X       = IN_X_HALF - BOSS_OD / 2.0 + BOSS_MERGE          # = 16.09
BOSS_ZS      = (WALL + BOSS_OD / 2.0 - BOSS_MERGE,              # = 5.00  above the board
                HEIGHT - WALL - BOSS_OD / 2.0 + BOSS_MERGE)     # = 44.80 below it

# The bezel's locating pocket. This is the "snug" part: the pocket and the window are on the SAME
# part, so there is no tolerance stack between where the board sits and where the hole is. On the
# old three-part design they were on different parts and every joint between them added error.
POCKET_CLR   = 0.15    # per side, around the board's outline
POCKET_D     = 2.0     # how deep the board's front edge sits into the bezel

# --- USB-C notch, right-hand wall -------------------------------------------------------------
# Generous on purpose. Unlike the screen window, nothing here needs precision: it clears a cable
# overmold, which is bigger and less well specified than the receptacle.
USB_SLOT_W  = USB_WIDTH + 3.0                                  # = 12.00, in Z
# ⚠️ The notch grows FORWARD, not backward. Backward is blocked by the middle plane, which sits
# 0.5 mm behind the connector and left only 4.8 mm of usable opening — under a plug's ~7 mm
# overmold. Forward is free all the way to the bezel's back face, so that is where the room is.
USB_SLOT_Y0 = BODY_Y0                                          # = 3.00
USB_SLOT_Y1 = BOARD_Y_REAR + 2.5                               # = 14.80
USB_SLOT_ZC = PCB_TOP_Z + USB_OFF_Z                            # = 26.16, the receptacle centreline

OUT_R = 3.0            # outer vertical corner radius


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
