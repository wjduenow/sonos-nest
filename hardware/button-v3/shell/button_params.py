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
COMP_Z_MAX   = STACK_TOTAL - DISP_STACK                        # = 3.00
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
DISP_OFF_X   = MEASURE("PCB left edge (USB-C on the RIGHT) -> lit area left edge")
DISP_OFF_Z   = MEASURE("PCB top edge -> lit area top edge")
WINDOW_CLR   = 0.6     # window is the lit area + this per side. Generous on purpose: a window
                       # that crops the display is unfixable without a reprint, whereas a slightly
                       # loose one only shows a sliver of black bezel.

# --- USB-C ------------------------------------------------------------------------------------
# On a SHORT edge (the 20.32 one), so it exits the LEFT or RIGHT wall of this landscape box.
# CONFIRMED 2026-09-09: the USB-C is on the RIGHT short edge with the screen upright, so the
# cable exits the right-hand wall and the chin above is on that same side.
USB_ON_RIGHT = True
USB_PROTRUDE = 2.0     # MEASURED 2026-09-08 — shell overhang past the board edge
USB_WIDTH    = MEASURE("USB-C shell width")
USB_HEIGHT   = MEASURE("USB-C shell height")
USB_OFF_Z    = MEASURE("PCB top edge -> USB-C shell centreline")

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
HOLES = MEASURE("the four (x, z) hole centres, measured from the PCB's top-left corner")

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
