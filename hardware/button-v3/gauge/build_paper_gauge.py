"""1:1 paper target sheet for locating the ESP32-S3-LCD-1.47B's mounting holes.

The trick: the eyelets are THROUGH-holes. Lay the board on this sheet with its outline aligned to
the printed outline, then look down through each eyelet — it is a ~2 mm window onto the paper. A
bullseye printed at the position the model currently believes in will appear centred if the model
is right, and visibly off if it is not, with rings every 0.25 mm to read the error off directly.

That beats calipers for the same reason the plastic gauge does: the centre of a hole is not a
feature a caliper jaw can touch, and measuring to its near edge is what produced the bogus
"1 and 33" reading earlier. It beats the plastic gauge on turnaround — seconds, not minutes.

⚠️ PRINT AT EXACTLY 100%. "Fit to page" / "Shrink oversized pages" silently scales by a few
percent, which is the same order as the error being hunted. The sheet carries a 100 mm check bar;
measure it before trusting anything else on the page.

  conda run -n img23d python build_paper_gauge.py    ->  hole_gauge.pdf
"""
import sys, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shell'))
import button_params as P

MM = 1 / 25.4
PAGE_W, PAGE_H = 210, 297          # A4 portrait, in mm


def main():
    fig = plt.figure(figsize=(PAGE_W * MM, PAGE_H * MM))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, PAGE_W); ax.set_ylim(0, PAGE_H)
    ax.set_aspect('equal'); ax.axis('off')

    # --- scale check, first thing on the page ---------------------------------------------
    bx, by = 20, PAGE_H - 25
    ax.plot([bx, bx + 100], [by, by], color='black', lw=1.2, solid_capstyle='butt')
    for i in range(11):
        h = 3 if i % 5 == 0 else 1.8
        ax.plot([bx + i * 10, bx + i * 10], [by, by + h], color='black', lw=0.8)
        if i % 5 == 0:
            ax.text(bx + i * 10, by + 4, f"{i*10}", ha='center', fontsize=6)
    ax.text(bx, by - 5, "MEASURE THIS BAR: it must be exactly 100 mm. If it is not, the page was "
                        "scaled — reprint at 100% / Actual size.", fontsize=7.5, va='top')

    # --- the board outline, at 1:1 ---------------------------------------------------------
    ox, oy = 30, PAGE_H - 55           # top-left corner of the board outline on the page
    bw, bh = P.PCB_W, P.PCB_H
    ax.add_patch(FancyBboxPatch((ox, oy - bh), bw, bh,
                                boxstyle=f"round,pad=0,rounding_size={P.PCB_CORNER}",
                                fill=False, ec='black', lw=0.5))
    ax.text(ox, oy + 4, "Lay the PCB here, component side UP, edges on the outline.",
            fontsize=8, weight='bold')
    ax.text(ox + bw, oy - bh - 3, "USB-C this end →", fontsize=7, ha='right', va='top')

    # --- a bullseye at each modelled hole position -----------------------------------------
    for hx, hz in P.HOLES:
        if True:
            cx, cy = ox + hx, oy - hz
            for r in [0.25 * k for k in range(1, 9)]:          # rings every 0.25 mm out to 2.0
                ax.add_patch(Circle((cx, cy), r, fill=False, ec='black',
                                    lw=0.9 if abs(r - 1.0) < 1e-6 else 0.25))
            ax.plot([cx - 2.6, cx + 2.6], [cy, cy], color='black', lw=0.25)
            ax.plot([cx, cx], [cy - 2.6, cy + 2.6], color='black', lw=0.25)

    # --- a 10x magnified legend ------------------------------------------------------------
    # The target is 2 mm across at 1:1, which is correct but hard to describe in words. Drawing
    # the three cases you might see, ten times up, turns "is it off?" into a comparison.
    K = 10.0
    lx, ly = 28, oy - bh - 72
    for i, (off, label) in enumerate(((0.0, "CENTRED\nhole is right"),
                                      (0.5, "0.5 mm off\n(2 rings)"),
                                      (1.0, "1.0 mm off\n(4 rings)"))):
        gx = lx + i * 52
        for r in [0.25 * k for k in range(1, 9)]:
            ax.add_patch(Circle((gx, ly), r * K, fill=False, ec='black',
                                lw=1.6 if abs(r - 1.0) < 1e-6 else 0.4))
        ax.plot([gx - 26, gx + 26], [ly, ly], color='black', lw=0.4)
        ax.plot([gx, gx], [ly - 26, ly + 26], color='black', lw=0.4)
        # the eyelet bore, drawn where it would appear: 2.0 mm across, offset by `off`
        ax.add_patch(Circle((gx + off * K, ly), (P.HOLE_D / 2) * K, fill=False,
                            ec='#b00000', lw=2.2, ls=(0, (4, 2))))
        ax.text(gx, ly - 32, label, fontsize=7.5, ha='center', va='top', family='monospace')
    ax.text(lx - 6, ly + 34,
            "WHAT YOU ARE LOOKING FOR  (shown 10x; red dashed = the eyelet's bore as you see it)",
            fontsize=8, weight='bold')

    # --- what the sheet is claiming, so a future reader can tell which build it came from ---
    ax.text(ox, oy - bh - 12,
            f"Modelled hole centres, from the board's top-left corner:\n"
            + "".join(f"   ({x:6.2f}, {z:5.2f})\n" for x, z in P.HOLES) +
            f"They are a TRAPEZOID: pitch {P.HOLE_Z_PITCH_USB:.2f} at the USB-C end,\n"
            f"{P.HOLE_Z_PITCH_FAR:.2f} at the far end, {P.HOLE_X_PITCH:.2f} along the board.\n"
            f"Board outline {P.PCB_W} x {P.PCB_H}. Rings are 0.25 mm apart.\n"
            f"The HEAVY ring is 1.0 mm radius = the M2 bore's own edge: if a hole is modelled\n"
            f"correctly the heavy ring sits exactly under the eyelet's rim, all the way round.",
            fontsize=7.5, va='top', family='monospace')

    ax.text(20, 86,
            "HOW TO READ IT\n\n"
            "1. Check the 100 mm bar above before anything else.\n"
            "2. Lay the board on the outline, aligned by its EDGES, not by the holes.\n"
            "3. Look straight down through each eyelet — it is a window onto the paper.\n"
            "4. The HEAVY ring should sit exactly under the eyelet's rim, all the way round.\n"
            "   Gap on one side and hidden on the other = that hole is off, that way.\n"
            "   Count the 0.25 mm rings across the gap to size the error.\n\n"
            "WHAT THE ANSWER MEANS\n\n"
            "   All four off the same way  -> the pattern needs shifting (an offset).\n"
            "   Off in opposite directions -> the pitch is wrong, not the position.\n"
            "   Only one off               -> re-check the board is square on the outline.\n\n"
            "Report the direction and ring count for each of the four and the model can be\n"
            "corrected without anyone having to find the centre of a hole with calipers.",
            fontsize=8, va='top', family='monospace')

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hole_gauge.pdf")
    fig.savefig(out)
    print(f"  {out}")
    print("  A4, 1:1. Holes at " + "  ".join(f"({x:.2f},{z:.2f})" for x, z in P.HOLES))


if __name__ == "__main__":
    main()
