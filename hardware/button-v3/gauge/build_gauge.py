"""Hole-position gauge for the ESP32-S3-LCD-1.47B.

A flat plate cut to the PCB's EXACT outline (36.37 x 20.32, corners rounded) carrying the four
mounting holes as currently modelled. Lay it on the board, line up the edges, and look at the
eyelets through the holes: any error in a hole position shows up directly as an eyelet sitting
off-centre in its hole, in a direction you can see and a magnitude you can judge against the ring.

⚠️ WHY THIS RATHER THAN CALIPERS. The centre of a hole is not a feature a caliper jaw can touch,
which is exactly how the first measuring pass reported "1 and 33" — those were 1.97 and 33.97 read
to the NEAR EDGE of the eyelet, each one hole-radius short. Referencing the board OUTLINE instead
removes the need to find a centre at all: the plate's edges do the locating, and the eye is very
good at spotting a circle that is not concentric with another circle.

Three variants are produced:

  gauge_fit.stl    holes at HOLE_CLR_D (2.6) — the real clearance. Does the plate drop on?
  gauge_check.stl  holes at 3.6 — deliberately loose, so it ALWAYS drops on and you can see how
                   far off each eyelet sits and which way. This is the diagnostic one.
  gauge_tight.stl  holes at 2.2 — barely over the M2 thread. If this one seats, the pattern is
                   right to within about 0.1 mm and nothing needs changing.

Print flat, no supports, 0.2 mm layers. ~2 g and a couple of minutes each.

  conda run -n img23d python build_gauge.py
"""
import sys, os
import numpy as np
import trimesh
from shapely.geometry import box as sbox

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shell'))
import button_params as P

T   = 1.6          # plate thickness — stiff enough not to bow, thin enough to see through a hole
SEG = P.SEG


def plate(hole_d):
    poly = (sbox(0, 0, P.PCB_W, P.PCB_H)
            .buffer(-P.PCB_CORNER, resolution=SEG // 4)
            .buffer(P.PCB_CORNER, resolution=SEG // 4))
    m = trimesh.creation.extrude_polygon(poly, height=T)

    cutters = []
    for x in (P.HOLE_X1, P.HOLE_X2):
        for z in (P.HOLE_Z1, P.HOLE_Z2):
            c = trimesh.creation.cylinder(radius=hole_d / 2, height=T + 2, sections=SEG)
            c.apply_translation((x, P.PCB_H - z, T / 2))   # z is from the TOP edge; plate Y is up
            cutters.append(c)

    # A notch on the USB-C edge, so the plate can only be laid on one way round. Without it the
    # gauge is nearly symmetric and can be flipped, which would hide an asymmetric error — the
    # very thing this is built to find.
    n = trimesh.creation.box(extents=(3.0, 2.0, T + 2))
    n.apply_translation((P.PCB_W, P.PCB_H / 2, T / 2))
    cutters.append(n)

    return trimesh.boolean.difference([m] + cutters, engine='manifold')


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    print(f"  outline    {P.PCB_W} x {P.PCB_H} mm, {T} thick, notch marks the USB-C edge")
    print(f"  holes at   x {P.HOLE_X1} / {P.HOLE_X2}   z {P.HOLE_Z1} / {P.HOLE_Z2}  (from top-left)")
    for name, d in (("fit", P.HOLE_CLR_D), ("check", 3.6), ("tight", 2.2)):
        m = plate(d)
        f = f"gauge_{name}.stl"
        m.export(f)
        print(f"  {f:18} holes {d} mm   watertight={m.is_watertight}  {m.volume/1000:.2f}cm3")
