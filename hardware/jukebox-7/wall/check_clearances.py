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


PCB_RECT = (P.PCB_X0, P.PCB_Y0, P.PCB_X1, P.PCB_Y1)
hx0, hy0 = P.PCB_X0 + P.HOLE_INSET, P.PCB_Y0 + P.HOLE_INSET
PCB_BOSSES = [(hx0, hy0), (hx0 + P.HOLE_DX, hy0),
              (hx0, hy0 + P.HOLE_DY), (hx0 + P.HOLE_DX, hy0 + P.HOLE_DY)]

print("\n== face screws must clear the PCB entirely (their bosses run full height) ==")
for i, (x, y) in enumerate(P.FSCREWS):
    r = P.FSCREW_BOSS / 2.0
    clear = not any(in_rect(p, PCB_RECT) for p in
                    [(x - r, y - r), (x + r, y - r), (x - r, y + r), (x + r, y + r)])
    check(f"face screw {i} ({x:.1f},{y:.1f})", clear,
          "outside the PCB footprint" if clear else "OVERLAPS THE PCB")

print("\n== face screws vs PCB mounting bosses ==")
for i, f in enumerate(P.FSCREWS):
    for j, b in enumerate(PCB_BOSSES):
        if ((f[0] - b[0]) ** 2 + (f[1] - b[1]) ** 2) ** 0.5 < 40:
            circles(f"face screw {i}", f, P.FSCREW_BOSS, f"PCB boss {j}", b, P.BOSS_OD)

print("\n== face screws vs keyholes ==")
for i, f in enumerate(P.FSCREWS):
    for kx in P.KEY_X:
        # widest part of the keyhole cut, plus the head-relief buffer
        circles(f"face screw {i}", f, P.FSCREW_BOSS,
                f"keyhole x={kx:.0f}", (kx, P.KEY_ENTRY_CY), P.KEY_ENTRY_D + 3.0)

print("\n== face screws vs the USB-C plate and the knob standoffs ==")
uc = (P.UC_CX - P.UC_REC_OFF - P.UC_PLATE_T / 2.0, P.UC_CY)
for i, f in enumerate(P.FSCREWS):
    circles(f"face screw {i}", f, P.FSCREW_BOSS, "USB-C plate", uc, P.UC_H)
for i, f in enumerate(P.FSCREWS):
    for dx in (-P.KNOB_HOLE_DX / 2.0, P.KNOB_HOLE_DX / 2.0):
        for dy in (-P.KNOB_HOLE_DY / 2.0, P.KNOB_HOLE_DY / 2.0):
            circles(f"face screw {i}", f, P.FSCREW_BOSS, "knob standoff",
                    (P.COL_CX + dx, P.DIAL_CY + dy), P.KNOB_STANDOFF_D)

print("\n== control column: everything must fit between the walls ==")
check("knob board width", P.COL_CX - P.KNOB_W / 2 >= P.COL_X0 and
      P.COL_CX + P.KNOB_W / 2 <= P.COL_X1,
      f"{P.KNOB_W} mm board in a {P.COL_W:.1f} mm column "
      f"({P.COL_CX - P.KNOB_W/2:.1f}..{P.COL_CX + P.KNOB_W/2:.1f})")
check("dial hole", P.COL_CX - (P.DIAL_D + P.DIAL_CLR) / 2 >= P.COL_X0,
      f"Ø{P.DIAL_D + P.DIAL_CLR} at x={P.COL_CX:.1f}")
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
for kx in P.KEY_X:
    top = P.KEY_ENTRY_CY + P.KEY_ENTRY_D / 2
    bot = P.KEY_ENTRY_CY - P.KEY_DROP - 1.5          # incl. head-relief buffer
    check(f"keyhole x={kx:.0f} top", top <= P.FACE_H - 1.0,
          f"reaches y={top:.2f} of {P.FACE_H}")
    check(f"keyhole x={kx:.0f} bottom", bot >= P.PCB_Y1,
          f"relief reaches y={bot:.2f}, PCB top is {P.PCB_Y1:.2f}",
          tight=(0 <= bot - P.PCB_Y1 < 1.0))

print("\n== depth ==")
check("USB-C headroom", P.UC_Z1 <= P.GLASS_Z, f"board rear edge z={P.UC_Z1}, face inner z={P.GLASS_Z}")
check("knob shaft engagement", P.KNOB_TIP_Z > P.DEPTH + 5,
      f"tip z={P.KNOB_TIP_Z}, face outer z={P.DEPTH} -> {P.KNOB_TIP_Z - P.DEPTH:.1f} mm into the cap")
check("knob standoff above floor", P.KNOB_PCB_Z > P.FLOOR_Z,
      f"standoff top z={P.KNOB_PCB_Z:.2f}, floor z={P.FLOOR_Z}")

print(f"\n{len(fails)} failure(s), {len(warns)} warning(s)")
for f in fails:
    print("  FAIL " + f)
sys.exit(1 if fails else 0)
