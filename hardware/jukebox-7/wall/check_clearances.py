#!/usr/bin/env python3
"""
Assert that nothing inside the case collides with anything else.

Written after a face-plate screw boss was found sitting on top of the PCB's
bottom-left mounting boss -- and, worse, underneath the PCB itself. That class of
mistake is invisible in an STL until you look into the corner, so it gets checked
here instead of by eye.

    conda run -n img23d python check_clearances.py     # exit 1 on any failure
"""
import sys
import case_params as P

fails, warns = [], []


def check(name, ok, detail, tight=False):
    (warns if (ok and tight) else fails if not ok else []).append(f"{name}: {detail}")
    print(f"  [{'WARN' if ok and tight else 'PASS' if ok else 'FAIL'}] {name}: {detail}")


def circles(an, a, ad, bn, b, bd, minimum=0.0):
    d = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
    gap = d - (ad + bd) / 2.0
    check(f"{an} vs {bn}", gap >= minimum,
          f"centres {d:.2f}, edge gap {gap:+.2f} mm", tight=(0 <= gap < 1.0))


def in_rect(pt, r):
    return r[0] <= pt[0] <= r[2] and r[1] <= pt[1] <= r[3]


def circle_vs_rect(cn, c, cd, rn, r, minimum=0.0):
    """Boards are rectangles, not circles. Approximating a 3 x 26 mm plate as a
    circle produced a false collision -- and a checker that cries wolf gets ignored."""
    qx = min(max(c[0], r[0]), r[2])
    qy = min(max(c[1], r[1]), r[3])
    d = ((c[0] - qx) ** 2 + (c[1] - qy) ** 2) ** 0.5
    gap = d - cd / 2.0
    check(f"{cn} vs {rn}", gap >= minimum, f"edge gap {gap:+.2f} mm",
          tight=(0 <= gap < 1.0))


def board_rects():
    pa, pb = sorted((P.UC_FACE_X, P.UC_FACE_X - P.UC_BOARD_SIDE * P.UC_PLATE_T))
    ba, bb = sorted((P.UC_FACE_X, P.UC_BOARD_X))
    return {
        "knob board": (P.COL_CX - P.KNOB_W / 2, P.DIAL_CY - P.KNOB_H / 2,
                       P.COL_CX + P.KNOB_W / 2, P.DIAL_CY + P.KNOB_H / 2),
        "expander board": (P.EXP_CX - P.EXP_W / 2, P.EXP_CY - P.EXP_H / 2,
                           P.EXP_CX + P.EXP_W / 2, P.EXP_CY + P.EXP_H / 2),
        "USB-C plate": (pa, P.UC_CY - P.UC_H / 2 - 2.5, pb, P.UC_CY + P.UC_H / 2 + 2.5),
        "USB-C board": (ba, P.UC_CY - P.UC_H / 2, bb, P.UC_CY + P.UC_H / 2),
    }


PCB_RECT = (P.PCB_X0, P.PCB_Y0, P.PCB_X1, P.PCB_Y1)
hx0, hy0 = P.PCB_X0 + P.HOLE_INSET, P.PCB_Y0 + P.HOLE_INSET
PCB_BOSSES = [(hx0, hy0), (hx0 + P.HOLE_DX, hy0),
              (hx0, hy0 + P.HOLE_DY), (hx0 + P.HOLE_DX, hy0 + P.HOLE_DY)]

print("\n== magnet SPIGOTS must clear the PCB (they descend into display-glass space) ==")
for i, (x, y) in enumerate(P.MAGNETS):
    r = P.MAG_SPIGOT_D / 2.0
    over = (P.PCB_X0 - r < x < P.PCB_X1 + r) and (P.PCB_Y0 - r < y < P.PCB_Y1 + r)
    margin = min(abs(y - P.PCB_Y0), abs(y - P.PCB_Y1)) - r
    check(f"magnet spigot {i} ({x:.1f},{y:.1f})", not over,
          f"{margin:+.2f} mm off the PCB edge" if not over else "OVERLAPS THE PCB",
          tight=(0 <= margin < 0.4))
    # NB: wall thickness is set by the BORE, not by the spigot -- the spigot is part of
    # the face plate and removes nothing. Checked under "magnets" below.

print("\n== magnet blocks vs PCB mounting bosses ==")
for i, f in enumerate(P.MAGNETS):
    for j, b in enumerate(PCB_BOSSES):
        if ((f[0] - b[0]) ** 2 + (f[1] - b[1]) ** 2) ** 0.5 < 40:
            circles(f"magnet block {i}", f, (2 * P.MAG_BLOCK_HW), f"PCB boss {j}", b, P.BOSS_OD)

print("\n== magnet blocks vs keyholes ==")
for i, f in enumerate(P.MAGNETS):
    for kx in P.KEY_X:
        # widest part of the keyhole cut, plus the head-relief buffer
        circles(f"magnet block {i}", f, (2 * P.MAG_BLOCK_HW),
                f"keyhole x={kx:.0f}", (kx, P.KEY_ENTRY_CY), P.KEY_ENTRY_D + 3.0)

print("\n== magnet blocks vs every mounted board ==")
for i, f in enumerate(P.MAGNETS):
    for name, r in board_rects().items():
        if abs(f[0] - (r[0] + r[2]) / 2) < 60 and abs(f[1] - (r[1] + r[3]) / 2) < 60:
            circle_vs_rect(f"magnet block {i}", f, (2 * P.MAG_BLOCK_HW), name, r)

print("\n== boards must not overlap each other in XY unless they differ in Z ==")
rs = board_rects()
for an, bn in (("knob board", "USB-C plate"), ("knob board", "USB-C board"),
               ("expander board", "USB-C plate"), ("expander board", "USB-C board"),
               ("knob board", "expander board")):
    a, b = rs[an], rs[bn]
    sep = max(a[0] - b[2], b[0] - a[2], a[1] - b[3], b[1] - a[3])
    check(f"{an} vs {bn}", sep >= 0, f"separation {sep:+.2f} mm", tight=(0 <= sep < 1.0))

print("\n== USB-C screw access (the screws run along X) ==")
check("driver clearance beside the board", P.UC_ACCESS >= 12.0,
      f"{P.UC_ACCESS:.2f} mm of open column on the screw-head side",
      tight=(12.0 <= P.UC_ACCESS < 18.0))

print("\n== control column: everything must fit between the walls ==")
check("knob board width", P.COL_CX - P.KNOB_W / 2 >= P.COL_X0 and
      P.COL_CX + P.KNOB_W / 2 <= P.COL_X1,
      f"{P.KNOB_W} mm board in a {P.COL_W:.1f} mm column "
      f"({P.COL_CX - P.KNOB_W/2:.1f}..{P.COL_CX + P.KNOB_W/2:.1f})")
check("dial bushing hole", P.COL_CX - P.DIAL_HOLE_D / 2 >= P.COL_X0,
      f"Ø{P.DIAL_HOLE_D} at x={P.COL_CX:.1f}")
check("knob cap fits the column", P.COL_CX - P.KCAP_D / 2 >= P.COL_X0 and
      P.COL_CX + P.KCAP_D / 2 <= P.COL_X1,
      f"Ø{P.KCAP_D} cap spans {P.COL_CX - P.KCAP_D/2:.1f}..{P.COL_CX + P.KCAP_D/2:.1f}")
check("knob cap clears the top button row",
      P.DIAL_CY - P.KCAP_D / 2 > max(P.BTN_ROW_Y) + P.BTN_D / 2,
      f"gap {P.DIAL_CY - P.KCAP_D/2 - (max(P.BTN_ROW_Y) + P.BTN_D/2):+.2f} mm")
check("cap overhangs the face hole", P.KCAP_D > P.DIAL_HOLE_D + 6,
      f"{(P.KCAP_D - P.DIAL_HOLE_D) / 2:.1f} mm of overhang all round")
check("cap bore engages the shaft", P.KNOB_TIP_Z - (P.DEPTH + P.KCAP_GAP) >= 6.0,
      f"{P.KNOB_TIP_Z - (P.DEPTH + P.KCAP_GAP):.1f} mm of shaft in a {P.KCAP_BORE_H} mm bore")
check("cap bore is not bottomed out by the shaft",
      P.KCAP_BORE_H > P.KNOB_TIP_Z - (P.DEPTH + P.KCAP_GAP),
      f"{P.KCAP_BORE_H - (P.KNOB_TIP_Z - (P.DEPTH + P.KCAP_GAP)):.1f} mm of air above the tip")
check("bore round section covers the shaft's round part",
      P.KCAP_ROUND_H >= (P.KNOB_TIP_Z - P.KNOB_SHAFT_FLAT) - (P.DEPTH + P.KCAP_GAP),
      f"round for {P.KCAP_ROUND_H} mm; shaft is round for "
      f"{(P.KNOB_TIP_Z - P.KNOB_SHAFT_FLAT) - (P.DEPTH + P.KCAP_GAP):.1f} mm above the cap's underside")
for by in P.BTN_ROW_Y:
    check(f"button row y={by}", P.COL_CX - P.BTN_PITCH / 2 - P.BTN_D / 2 >= P.COL_X0,
          f"pitch {P.BTN_PITCH} spans "
          f"{P.COL_CX - P.BTN_PITCH/2 - P.BTN_D/2:.1f}..{P.COL_CX + P.BTN_PITCH/2 + P.BTN_D/2:.1f}")

print("\n== column stack: knob board / buttons / USB-C must not overlap in Y ==")
kb0 = P.DIAL_CY - P.KNOB_H / 2
btn_hi = max(P.BTN_ROW_Y) + (P.BTN_D + P.BTN_CLR) / 2
btn_lo = min(P.BTN_ROW_Y) - (P.BTN_D + P.BTN_CLR) / 2
uc_hi = P.UC_CY + P.UC_H / 2
check("knob board vs top button row", kb0 > btn_hi, f"gap {kb0 - btn_hi:+.2f} mm",
      tight=(0 < kb0 - btn_hi < 3))
check("bottom button row vs USB-C", btn_lo > uc_hi, f"gap {btn_lo - uc_hi:+.2f} mm",
      tight=(0 < btn_lo - uc_hi < 3))

print("\n== keyholes must stay inside the top band and off the PCB ==")
check("keyhole slot runs UP from the entry hole", P.KEY_SLOT_TOP > P.KEY_ENTRY_CY,
      "load-bearing closed end is above the entry -- the unit is lowered onto the screws")
for kx in P.KEY_X:
    top = P.KEY_SLOT_TOP + 1.5                       # slot end + head-relief buffer
    bot = P.KEY_ENTRY_CY - P.KEY_ENTRY_D / 2 - 1.5   # entry circle + buffer
    check(f"keyhole x={kx:.0f} top", top <= P.FACE_H - 1.0,
          f"relief reaches y={top:.2f} of {P.FACE_H}")
    check(f"keyhole x={kx:.0f} bottom", bot >= P.PCB_Y1,
          f"relief reaches y={bot:.2f}, PCB top is {P.PCB_Y1:.2f}",
          tight=(0 <= bot - P.PCB_Y1 < 1.0))

print("\n== USB-C port cutout ==")
# The board stands on edge, so the receptacle's LONG axis runs ALONG the column (Y) and
# its short axis (the board's normal) runs ACROSS it (X). Getting this backwards is what
# produced the first wrong cutout, so the mapping is asserted explicitly.
REC_LONG, REC_SHORT = 8.94, 3.26        # a USB-C receptacle body
check("port clears the receptacle across (short axis)", P.PORT_W > REC_SHORT + 1.0,
      f"{P.PORT_W} vs {REC_SHORT} -> {(P.PORT_W - REC_SHORT) / 2:.2f} mm each side",
      tight=((P.PORT_W - REC_SHORT) / 2 < 1.0))
check("port clears the receptacle along (long axis)", P.PORT_H > REC_LONG + 1.0,
      f"{P.PORT_H} vs {REC_LONG} -> {(P.PORT_H - REC_LONG) / 2:.2f} mm each side")
check("port's long axis follows the board's plane", P.PORT_H > P.PORT_W,
      "long axis along the column, as the on-edge board requires")
check("funnel stays inside the case",
      P.UC_CX + P.PORT_W / 2 + P.PORT_FUNNEL < P.FACE_W - 3 and
      P.UC_CY - P.PORT_H / 2 - P.PORT_FUNNEL > P.WALL,
      f"outer opening {P.PORT_W + 2*P.PORT_FUNNEL:.1f} x {P.PORT_H + 2*P.PORT_FUNNEL:.1f}, "
      f"spans x to {P.UC_CX + P.PORT_W/2 + P.PORT_FUNNEL:.1f}, "
      f"y from {P.UC_CY - P.PORT_H/2 - P.PORT_FUNNEL:.1f}")
check("port sits behind the breakout board",
      abs(P.UC_CY - P.UC_CY) < 0.01 and P.PORT_H < P.UC_H,
      f"port {P.PORT_H} within the {P.UC_H} mm board height")

print("\n== magnets ==")
check("spigot wall around the face magnet", (P.MAG_SPIGOT_D - P.MAG_POCKET_D) / 2 >= 0.7,
      f"{(P.MAG_SPIGOT_D - P.MAG_POCKET_D) / 2:.2f} mm of spigot around the Ø{P.MAG_D_FACE} disc")
check("shell magnet stays located in the bore",
      0.2 <= (P.MAG_BORE_D - P.MAG_D_SHELL) / 2 <= 0.45,
      f"{(P.MAG_BORE_D - P.MAG_D_SHELL) / 2:.2f} mm a side -- loosening the SPIGOT must not "
      f"loosen the magnet, so the bore is fixed and the spigot shrinks")
check("spigot fit absorbs print-scale variation",
      P.MAG_SPIGOT_FIT / 2 >= 0.35,
      f"{P.MAG_SPIGOT_FIT/2:.2f} mm a side vs {(max(P.MAG_X)-min(P.MAG_X))*0.002:.2f} mm of "
      f"drift at 0.2% scale over {max(P.MAG_X)-min(P.MAG_X):.0f} mm")
check("shell bore is a single diameter", P.MAG_BORE_D > P.MAG_D_SHELL,
      f"Ø{P.MAG_BORE_D} bore takes the Ø{P.MAG_D_SHELL} disc with "
      f"{(P.MAG_BORE_D - P.MAG_D_SHELL) / 2:.2f} mm a side -- no step to glue into")
for i, (x, y) in enumerate(P.MAGNETS):
    r = P.MAG_BORE_D / 2.0
    inner = ((P.MAG_BLOCK_BOT_Y1 - (y + r)) if y < P.FACE_H / 2
             else ((y - r) - P.MAG_BLOCK_TOP_Y0))
    outer = (y - r) if y < P.FACE_H / 2 else (P.FACE_H - (y + r))
    check(f"magnet bore {i} walls", min(inner, outer) >= 1.0,
          f"{outer:.2f} mm outboard, {inner:.2f} mm inboard",
          tight=(1.0 <= min(inner, outer) < 1.2))
check("material over the magnet", P.DEPTH - (P.MAG_SPIGOT_Z0 + P.MAG_POCKET_H) >= 2.0,
      f"{P.DEPTH - (P.MAG_SPIGOT_Z0 + P.MAG_POCKET_H):.2f} mm from disc to the front face")
check("face seats on the rim, not on the magnets", P.MAG_AIRGAP >= 0.3,
      f"spigot tip z={P.MAG_SPIGOT_Z0}, shell magnet face z={P.MAG_MATE_Z} "
      f"-> {P.MAG_AIRGAP:.2f} mm of slack before anything bottoms out")
check("spigot still engages for registration", P.FACE_Z0 - P.MAG_SPIGOT_Z0 >= 3.0,
      f"{P.FACE_Z0 - P.MAG_SPIGOT_Z0:.2f} mm of spigot inside a "
      f"{P.FACE_Z0 - (P.MAG_MATE_Z - P.MAG_T):.2f} mm bore")
check("shell has depth under the pocket", P.MAG_MATE_Z - P.MAG_POCKET_H > P.FLOOR_Z,
      f"pocket floor z={P.MAG_MATE_Z - P.MAG_POCKET_H:.2f}, case floor z={P.FLOOR_Z}")

print("\n== screen opening (wrap-around: clears the MODULE, not the lit area) ==")
import build_face as BF
sx0, sy0, sx1, sy1 = BF.screen_rect()
gx0, gy0 = P.PCB_X0 + P.GLASS_X0, P.PCB_Y0 + P.GLASS_Y0
check("opening clears the module", sx0 < gx0 and sy0 < gy0 and
      sx1 > gx0 + P.GLASS_W and sy1 > gy0 + P.GLASS_H,
      f"{sx1-sx0:.2f} x {sy1-sy0:.2f} opening for a {P.GLASS_W} x {P.GLASS_H} module "
      f"-> {P.SCREEN_CLR/2:.2f} mm a side")
check("module borders reconcile with the PCB",
      abs(2 * P.GLASS_X0 + P.GLASS_W - P.PCB_W) < 0.05 and
      abs(2 * P.GLASS_Y0 + P.GLASS_H - P.PCB_H) < 0.05,
      f"{P.GLASS_X0}+{P.GLASS_W}+{P.GLASS_X0} = {2*P.GLASS_X0+P.GLASS_W:.2f} vs PCB {P.PCB_W}; "
      f"{P.GLASS_Y0}+{P.GLASS_H}+{P.GLASS_Y0} = {2*P.GLASS_Y0+P.GLASS_H:.2f} vs PCB {P.PCB_H}")
for nm, lo, hi in (("left", 0.0, sx0), ("right", sx1, P.FACE_W),
                   ("bottom", 0.0, sy0), ("top", sy1, P.FACE_H)):
    check(f"face frame {nm}", hi - lo >= 8.0, f"{hi - lo:.2f} mm of plate")
for i, (x, y) in enumerate(P.MAGNETS):
    r = P.MAG_SPIGOT_D / 2
    clear = (x + r < sx0 or x - r > sx1 or y + r < sy0 or y - r > sy1)
    check(f"magnet spigot {i} clears the opening", clear, "lands in solid face plate")

print("\n== plugged cables (the real rear-most feature) ==")
relief_floor = P.FLOOR_Z - P.RELIEF_D
cable_z = P.DEPTH - P.PLUGGED_DEPTH
check("plugged J10 cable clears the rear wall", cable_z >= relief_floor,
      f"cable back face z={cable_z:.2f}, J10 relief floor z={relief_floor:.2f} "
      f"-> {cable_z - relief_floor:+.2f} mm",
      tight=(0 <= cable_z - relief_floor < 1.0))
check("plugged cable is inside the case at all", cable_z >= 0.0,
      f"cable back face z={cable_z:.2f} vs the rear outer plane z=0")
check("bare board still clears its floor", P.REAR_CLR >= 0.5,
      f"REAR_CLR {P.REAR_CLR} behind the tallest bare component")

print("\n== pry notch ==")
for mx in P.MAG_X:
    ov = (min(mx + P.MAG_BLOCK_HW, P.PRY_X + P.PRY_W / 2)
          - max(mx - P.MAG_BLOCK_HW, P.PRY_X - P.PRY_W / 2))
    check(f"pry notch clears the magnet block at x={mx:.0f}", ov <= 0,
          f"{-ov:.1f} mm clear" if ov <= 0 else f"OVERLAPS BY {ov:.1f} mm -- breaches the bore")

print("\n== rear-face clearances ==")
check("left clearance for the J10 cable", P.CLR_LEFT >= 8.0,
      f"{P.CLR_LEFT:.2f} mm from the PCB's left edge to the wall; the XH housing plugs in "
      f"horizontally and protrudes past the board")
check("J10 sits in that gap", P.PCB_X0 + P.J10_X - P.WALL > 8.0,
      f"J10 at x={P.PCB_X0 + P.J10_X:.2f}, wall inside at {P.WALL}")
check("magnet block clears the board below", P.PCB_Y0 - P.MAG_BLOCK_BOT_Y1 >= 1.5,
      f"{P.PCB_Y0 - P.MAG_BLOCK_BOT_Y1:.2f} mm gap: block ends y={P.MAG_BLOCK_BOT_Y1:.2f}, "
      f"board starts y={P.PCB_Y0:.2f}")
check("magnet block clears the board above", P.MAG_BLOCK_TOP_Y0 - P.PCB_Y1 >= 1.5,
      f"{P.MAG_BLOCK_TOP_Y0 - P.PCB_Y1:.2f} mm gap: board ends y={P.PCB_Y1:.2f}, "
      f"block starts y={P.MAG_BLOCK_TOP_Y0:.2f}")
check("PCB clearance in Y (band to board)", P.CLR_Y >= 2.0,
      f"{P.CLR_Y:.2f} mm above and below the board")
for nm, conn, rect in (("I2C  J13", (P.PCB_X0 + P.J13_X, P.PCB_Y0 + P.J13_Y), P.I2C_RELIEF),
                       ("PWR  J10", (P.PCB_X0 + P.J10_X, P.PCB_Y0 + P.J10_Y), P.J10_RELIEF)):
    check(f"{nm} relief covers the connector",
          rect[0] <= conn[0] <= rect[2] and rect[1] <= conn[1] <= rect[3],
          f"connector at ({conn[0]:.2f}, {conn[1]:.2f}), relief "
          f"x {rect[0]:.2f}..{rect[2]:.2f}  y {rect[1]:.2f}..{rect[3]:.2f}")
    check(f"{nm} relief clears the keyholes",
          all(not (rect[0] - 4 < kx < rect[2] + 4 and rect[1] - 4 < P.KEY_ENTRY_CY < rect[3] + 4)
              for kx in P.KEY_X),
          "no keyhole lands in the pocket")
check("reliefs leave rear wall", P.REAR_WALL - P.RELIEF_D >= 0.8,
      f"{P.REAR_WALL - P.RELIEF_D:.2f} mm of wall under the pockets")
check("clearance after relief", P.REAR_CLR + P.RELIEF_D >= 1.5,
      f"{P.REAR_CLR + P.RELIEF_D:.2f} mm from connector to floor there",
      tight=(P.REAR_CLR + P.RELIEF_D < 2.5))
check("glass front is flush with the face front", abs(P.DEPTH - P.GLASS_Z) < 1e-6,
      f"glass z={P.GLASS_Z}, face front z={P.DEPTH} -- wrap-around, no bezel over the glass")
check("shell stops where the face starts", abs(P.FACE_Z0 - (P.DEPTH - P.FACE_T)) < 1e-6,
      f"shell top z={P.FACE_Z0}, face plate {P.FACE_Z0}..{P.DEPTH} -- no perimeter interference")

print("\n== depth ==")
check("USB-C headroom", P.UC_Z1 <= P.FACE_Z0,
      f"board rear edge z={P.UC_Z1}, face underside z={P.FACE_Z0}")
check("knob shaft engagement", P.KNOB_TIP_Z > P.DEPTH + 5,
      f"tip z={P.KNOB_TIP_Z}, face outer z={P.DEPTH} -> {P.KNOB_TIP_Z - P.DEPTH:.1f} mm into the cap")
check("knob standoff above floor", P.KNOB_PCB_Z > P.FLOOR_Z,
      f"standoff top z={P.KNOB_PCB_Z:.2f}, floor z={P.FLOOR_Z}")

print(f"\n{len(fails)} failure(s), {len(warns)} warning(s)")
for f in fails:
    print("  FAIL " + f)
sys.exit(1 if fails else 0)
