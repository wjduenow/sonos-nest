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
PCB_T        = MEASURE("PCB thickness (nominally 1.6, but caliper a bare edge)")
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
# The display is on one face and everything else (ESP32-S3 module, USB-C, microSD socket) on the
# other, so the two directions are measured separately from the PCB's own faces.
DISP_PROUD   = MEASURE("display glass top surface above the PCB's display-side face")
COMP_Z_MAX   = MEASURE("tallest part on the component side above that face (likely the USB-C shell)")

# --- The display window ----------------------------------------------------------------------
# The LIT area, not the glass. ~17.4 x 32.4 mm is what 172 x 320 px at this panel's pitch works
# out to, but the offsets are what actually matter and cannot be derived at all.
DISP_ACT_W   = MEASURE("lit area width  (long axis, expect ~32.4)")
DISP_ACT_H   = MEASURE("lit area height (short axis, expect ~17.4)")
DISP_OFF_X   = MEASURE("PCB left edge -> lit area left edge")
DISP_OFF_Z   = MEASURE("PCB top edge  -> lit area top edge")
WINDOW_CLR   = 0.6     # window is the lit area + this per side. Generous on purpose: a window
                       # that crops the display is unfixable without a reprint, whereas a slightly
                       # loose one only shows a sliver of black bezel.

# --- USB-C ------------------------------------------------------------------------------------
# On a SHORT edge (the 20.32 one), so it exits the LEFT or RIGHT wall of this landscape box.
USB_PROTRUDE = MEASURE("how far the USB-C shell overhangs the board edge (0 if flush)")
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
BOSS_STANDOFF  = MEASURE("front wall inner face -> PCB front face; = DISP_PROUD + display air gap")
_STACK_Y       = MEASURE("BOSS_STANDOFF + PCB_T + COMP_Z_MAX + rear clearance")
IN_Y   = MEASURE("max(BUTTON_NUT_AC + 2*NUT_SOCKET_CLR, _STACK_Y)")   # ~19.5 once measured
OUT_Y  = MEASURE("IN_Y + FRONT_WALL + LID_T")                          # ~25.0

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
    print(f"  PCB            {PCB_W} x {PCB_H} mm")
    print(f"  header rows    {HDR_ROW_GAP} apart, {HDR_EDGE_OFF:.2f} from each long edge (derived)")
    print(f"  button keepout {BUTTON_KEEPOUT_Z:.2f} deep, {BUTTON_NUT_AC:.2f} across corners")
    print(f"  PCB top edge   z = {PCB_TOP_Z:.2f}")
    print(f"  OVERALL        {OUT_X:.2f} (X) x ? (Y, needs measurements) x {HEIGHT:.2f} (Z)")
    print()
    print("  Y is unresolved until the stack is measured — see README.md §2.")
