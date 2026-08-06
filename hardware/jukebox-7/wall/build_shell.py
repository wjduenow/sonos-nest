#!/usr/bin/env python3
"""
Build shell.stl -- the rear body of the sonos-jukebox 7" flush wall case.

Features:
  * rounded outer body, hollowed to a 2.5 mm rear wall and 3 mm side walls
  * PCB pocket with 4 self-tap bosses on the 170.90 x 98.00 hole pattern
  * 2 KEYHOLE slots in the top band -- the whole wall mount, adding 0 mm of depth
  * USB-C breakout cradle at the bottom of the control column, with an oversized
    funnelled port through the rear wall so the wall's cable hole need not be exact
  * a channel across the rear for the 2-wire power run to J10 (far LEFT)
  * 4 face-plate screw bosses

    conda run -n img23d python build_shell.py     # -> shell.stl

Geometry lives in case_params.py.  Frame: +X right, +Y up, +Z toward the viewer,
z = 0 is the rear plane against the wall.
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import cylinder, box, extrude_polygon
from trimesh.boolean import union, difference
from shapely.geometry import box as sbox
from shapely.ops import unary_union
import case_params as P


def rrect(x0, y0, x1, y1, r):
    """Rounded rectangle polygon in the XY plane."""
    return sbox(x0 + r, y0 + r, x1 - r, y1 - r).buffer(r, resolution=24)


def prism(poly, z0, z1):
    m = extrude_polygon(poly, height=z1 - z0)
    m.apply_translation([0, 0, z0])
    return m


def cyl(r, z0, z1, x, y, seg=P.SEG):
    m = cylinder(radius=r, height=z1 - z0, sections=seg)
    m.apply_translation([x, y, (z0 + z1) / 2.0])
    return m


def bx(x0, y0, z0, x1, y1, z1):
    m = box(extents=[x1 - x0, y1 - y0, z1 - z0])
    m.apply_translation([(x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0])
    return m


def keyhole_poly(cx):
    """Keyhole outline: entry circle at the BOTTOM, slot running UP by KEY_DROP.

    The unit is lowered onto fixed wall screws, so relative to the case the screw
    travels upward out of the entry hole into the slot, and the slot's closed upper
    end carries the load. See the orientation note in case_params.py -- this was
    built upside down once.
    """
    from shapely.geometry import Point
    entry = Point(cx, P.KEY_ENTRY_CY).buffer(P.KEY_ENTRY_D / 2.0, resolution=24)
    slot = sbox(cx - P.KEY_SLOT_W / 2.0, P.KEY_ENTRY_CY,
                cx + P.KEY_SLOT_W / 2.0, P.KEY_SLOT_TOP)
    return unary_union([entry, slot])


def build_shell():
    # ---- outer body ------------------------------------------------------------
    outer = rrect(0, 0, P.FACE_W, P.FACE_H, P.CASE_R)
    solid = prism(outer, 0.0, P.DEPTH)

    # ---- hollow it out: one cavity from the interior floor up to the face -------
    inner = rrect(P.WALL, P.WALL, P.FACE_W - P.WALL, P.FACE_H - P.WALL,
                  max(P.CASE_R - P.WALL, 1.0))
    cavity = prism(inner, P.FLOOR_Z, P.DEPTH)
    shell = difference([solid, cavity])

    adds, cuts = [], []

    # ---- PCB mounting bosses ---------------------------------------------------
    hx0 = P.PCB_X0 + P.HOLE_INSET
    hy0 = P.PCB_Y0 + P.HOLE_INSET
    holes = [(hx0, hy0), (hx0 + P.HOLE_DX, hy0),
             (hx0, hy0 + P.HOLE_DY), (hx0 + P.HOLE_DX, hy0 + P.HOLE_DY)]
    for (x, y) in holes:
        adds.append(cyl(P.BOSS_OD / 2.0, P.FLOOR_Z, P.PCB_BACK_Z, x, y))
        cuts.append(cyl(P.BOSS_PILOT / 2.0, P.FLOOR_Z - 1.0, P.PCB_BACK_Z, x, y, seg=32))

    # ---- magnet blocks for the face plate ---------------------------------------
    # A BLOCK, not a cylinder: the bands are only 7.75 / 10.25 mm of free strip, so a
    # round boss big enough to wall a 6 mm pocket would run into the PCB. The block
    # spans the band and merges with the side wall, which backs the pocket outward.
    # Each block takes the face plate's spigot (registration) over the magnet pocket.
    for (x, y) in P.MAGNETS:
        # NB the block stops MAG_BLOCK_GAP short of the board -- not at PCB_Y0/PCB_Y1,
        # which is what made it pinch the board however much the case grew.
        y0, y1 = ((P.WALL, P.MAG_BLOCK_BOT_Y1) if y < P.FACE_H / 2
                  else (P.MAG_BLOCK_TOP_Y0, P.FACE_H - P.WALL))
        adds.append(bx(x - P.MAG_BLOCK_HW, y0, P.FLOOR_Z,
                       x + P.MAG_BLOCK_HW, y1, P.GLASS_Z))
    for (x, y) in P.MAGNETS:
        # ONE straight bore, top to bottom: the upper part receives the face plate's
        # spigot, the 8 mm disc fills the bottom of the same hole. No step to glue into.
        cuts.append(cyl(P.MAG_BORE_D / 2.0,
                        P.MAG_MATE_Z - P.MAG_T, P.GLASS_Z + 0.01, x, y))

    # ---- pry notch --------------------------------------------------------------
    # Six magnet pairs meeting face to face hold hard; the plate needs a purchase point.
    # Bottom edge, hidden once the unit is on the wall.
    cuts.append(bx(P.PRY_X - P.PRY_W / 2.0, -1.0, P.GLASS_Z - 2.5,
                   P.PRY_X + P.PRY_W / 2.0, P.WALL + 0.5, P.GLASS_Z + 0.01))

    # ---- keyhole wall mount ----------------------------------------------------
    # Through the rear wall, plus a shallow relief inside so the captured screw head
    # has somewhere to sit while the unit slides down.
    for cx in P.KEY_X:
        kp = keyhole_poly(cx)
        cuts.append(prism(kp, -1.0, P.REAR_WALL + 0.01))
        cuts.append(prism(kp.buffer(1.5, resolution=16),
                          P.REAR_WALL, P.REAR_WALL + P.KEY_HEAD_CLR))

    # ---- USB-C breakout: a SINGLE mounting plate -------------------------------
    # The board screws flat onto one plate through its two post holes -- no slot to
    # spring it into, and it can be removed without dismantling anything. The plate sits
    # UC_REC_OFF to one side of the port centreline so the receptacle lands on the port.
    # It is a BARE plate -- no lip or roof over the board. Anything overhanging the
    # board's outer face is directly in the path of the screwdriver, because the two
    # screws run along X and can only be reached from the side.
    pa, pb = sorted((P.UC_FACE_X, P.UC_FACE_X - P.UC_BOARD_SIDE * P.UC_PLATE_T))
    py0, py1 = P.UC_CY - P.UC_H / 2.0 - 2.5, P.UC_CY + P.UC_H / 2.0 + 2.5
    adds.append(bx(pa, py0, P.FLOOR_Z, pb, py1, P.UC_Z1))
    # two M3 self-tap pilots, drilled along X into the plate
    for dy in (-P.UC_HOLE_CC / 2.0, +P.UC_HOLE_CC / 2.0):
        m = cylinder(radius=P.UC_PILOT / 2.0, height=P.UC_PLATE_T + 2.0, sections=32)
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
        m.apply_translation([P.UC_PLATE_CX, P.UC_CY + dy, P.UC_Z0 + P.UC_HOLE_OFF])
        cuts.append(m)

    # ---- rotary encoder board (Arduino Modulino Knob) --------------------------
    # Four standoffs on the datasheet's 32 x 16 hole pattern, centred on the dial.
    # Height is driven by KNOB_TIP_Z so the cap gets real shaft engagement.
    for dx in (-P.KNOB_HOLE_DX / 2.0, +P.KNOB_HOLE_DX / 2.0):
        for dy in (-P.KNOB_HOLE_DY / 2.0, +P.KNOB_HOLE_DY / 2.0):
            x, y = P.COL_CX + dx, P.DIAL_CY + dy
            adds.append(cyl(P.KNOB_STANDOFF_D / 2.0, P.FLOOR_Z, P.KNOB_PCB_Z, x, y))
            cuts.append(cyl(P.KNOB_PILOT / 2.0, P.KNOB_PCB_Z - 9.0, P.KNOB_PCB_Z + 1.0,
                            x, y, seg=32))

    # ---- button I/O expander (Adafruit PCF8574) --------------------------------
    # Flat on the floor under the button grid; the switches hang down from the face
    # plate well above it.
    for dx in (-P.EXP_HOLE_CC / 2.0, +P.EXP_HOLE_CC / 2.0):
        x, y = P.EXP_CX + dx, P.EXP_CY + P.EXP_HOLE_DY
        adds.append(cyl(P.EXP_HOLE_D / 2.0 + 1.6, P.FLOOR_Z, P.EXP_PCB_Z, x, y))
        cuts.append(cyl(P.EXP_PILOT / 2.0, P.EXP_PCB_Z - 6.0, P.EXP_PCB_Z + 1.0, x, y, seg=32))

    # ---- rear port + funnel ----------------------------------------------------
    port = bx(P.UC_CX - P.PORT_W / 2.0, P.UC_CY - P.PORT_H / 2.0, -1.0,
              P.UC_CX + P.PORT_W / 2.0, P.UC_CY + P.PORT_H / 2.0, P.REAR_WALL + 0.01)
    cuts.append(port)
    # outward funnel: a bigger opening on the wall face, tapering in
    f = P.PORT_FUNNEL
    funnel = bx(P.UC_CX - P.PORT_W / 2.0 - f, P.UC_CY - P.PORT_H / 2.0 - f, -1.0,
                P.UC_CX + P.PORT_W / 2.0 + f, P.UC_CY + P.PORT_H / 2.0 + f, 1.0)
    cuts.append(funnel)

    # ---- reliefs behind the rear-face connectors --------------------------------
    # J13 (I2C) and J10 (power) both sit on the PCB's rear face with only REAR_CLR
    # behind them, which is nothing once a plug is in. Relieved locally so the whole
    # case does not get deeper for two connectors.
    for (rx0, ry0, rx1, ry1) in (P.I2C_RELIEF, P.J10_RELIEF):
        cuts.append(bx(rx0, ry0, P.FLOOR_Z - P.RELIEF_D, rx1, ry1, P.FLOOR_Z + 0.01))

    # ---- power cable channel: breakout (right) -> J10 (far LEFT) ---------------
    # A recess in the interior floor so the pair does not get pinched under the PCB.
    ch_y = P.PCB_Y0 + P.J10_Y
    cuts.append(bx(P.PCB_X0 + P.J10_X - 4.0, ch_y - P.CABLE_CH_W / 2.0, P.FLOOR_Z - 1.2,
                   P.UC_CX, ch_y + P.CABLE_CH_W / 2.0, P.FLOOR_Z + 0.01))

    # ---- assemble --------------------------------------------------------------
    shell = union([shell] + adds)
    shell = difference([shell] + cuts)
    return shell


if __name__ == "__main__":
    m = build_shell()
    m.export("shell.stl")
    print(f"shell.stl  watertight={m.is_watertight}  volume={m.volume/1000:.1f} cm^3  "
          f"bbox={np.round(m.extents,2)}")
