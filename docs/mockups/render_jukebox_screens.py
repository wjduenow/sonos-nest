#!/usr/bin/env python3
"""Render the Sonos Jukebox on-glass UI to PNG, for the README showcase.

WHY THIS EXISTS. The jukebox is a 7" glossy IPS panel on a wall — photographing it produces
glare and moire that no amount of lighting fixes, and a soft/reflective photo undersells a UI
that is actually crisp. So the four screens in `docs/images/jukebox-*.png` are RENDERED, not
photographed, and this script is how. Every other unit in the README is a real photo.

FIDELITY IS THE WHOLE POINT — this is not a redesign. Each coordinate below is copied from
`src/units/sonos_jukebox/screens.cpp` (and its `ui_scale.h` tokens), with the source line noted
where it is not obvious. Colours are the design-system tokens the firmware compiles in; the
typeface is the very same `Montserrat-Medium.ttf` that LVGL generates its built-in
`lv_font_montserrat_*` from, so glyph shapes match the panel rather than merely resembling it.
If you change a layout constant in screens.cpp, change it here too or the README starts lying.

Two deliberate approximations, both unavoidable and both cosmetic:
  * ICONS. The device draws FontAwesome/Lucide glyphs from subsetted LVGL fonts (see
    lv_font_lucide_28.c). Those .c files are not renderable here, so the glyphs are drawn as
    vector primitives at the same size and position. Shapes match; hinting does not.
  * ALBUM ART. Real covers are copyrighted, so `_cover()` synthesises a warm abstract tile in
    the palette the design system specifies for placeholder art.

Content is fictional (invented artist/album/station names) so nothing here implies a
relationship with a real label, station or service.

Usage:
    python3 docs/mockups/render_jukebox_screens.py [--out docs/images] [--font <Montserrat.ttf>]

Requires Pillow. The font is found automatically from any PlatformIO LVGL checkout in the tree
(`.pio/libdeps/*/lvgl/scripts/built_in_font/Montserrat-Medium.ttf`) — build any env once and it
is there. `--font` overrides.
"""

from __future__ import annotations

import argparse
import glob
import math
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

# --- Design tokens — src/units/sonos_jukebox/ui_scale.h ---------------------------------------
BG        = "#0e0f12"   # JB_SCREEN_BG
ELEV      = "#191b20"   # JB_SCREEN_ELEV
ELEV2     = "#23262d"   # JB_SCREEN_ELEV_2
LINE      = "#2c3038"   # JB_SCREEN_LINE
TEXT      = "#f4f5f7"   # JB_TEXT
MUTED     = "#9aa0ab"   # JB_TEXT_MUTED
DIM       = "#6a7079"   # JB_TEXT_DIM
ACCENT    = "#e8892b"   # JB_ACCENT (amber)
ACCENT_INK = "#ffffff"  # JB_ACCENT_INK

R_MD, R_LG, R_XL = 14, 20, 28

# --- Geometry — screens.cpp:55-108 ------------------------------------------------------------
SCREEN_W, SCREEN_H = 1024, 600
RAIL_W, RAIL_BTN, RAIL_STEP = 96, 72, 86
PAD_X, PAD_TOP, PAD_BOT = 30, 22, 18
ART, GAP = 280, 34

CONTENT_X = RAIL_W + PAD_X                      # 126 — screens.cpp:2545
CONTENT_W = SCREEN_W - RAIL_W - PAD_X * 2       # 868

NP_ART_TOP  = (SCREEN_H - ART) // 2             # 160
NP_ART_BOT  = NP_ART_TOP + ART                  # 440
NP_TITLE_LH, NP_TITLE_H = 52, 104
NP_SMALL_H  = 15
NP_BADGE_Y  = NP_ART_TOP + 26                   # 186
NP_TITLE_Y  = NP_BADGE_Y + NP_SMALL_H + 13      # 214
NP_META_Y   = NP_TITLE_Y + NP_TITLE_H + 18      # 336
NP_TIMES_Y  = NP_ART_BOT - NP_SMALL_H           # 425
NP_TRACK_Y  = NP_TIMES_Y - 8 - 6                # 411

# Rooms — screens.cpp:515-529
RM_W        = CONTENT_W                         # 868
RM_BAR_Y, RM_BAR_H = 76, 80
RM_LIST_Y   = 192
RM_ROW_H, RM_ROW_PITCH = 62, 66
RX_CHECK, RC_SZ = 8, 28
RX_NAME, RN_W   = 52, 156
RX_BADGE        = 214
RX_VOL, RV_W    = 272, 300
RX_VPCT         = 584
RX_MINUS, RX_PLUS, R_STEP_SZ = 708, 760, 44
RX_PLAY, R_PLAY_SZ = 812, 48

SAVER_W, SAVER_H = 760, 220                     # screens.cpp:2202

# LVGL's Montserrat carries U+2022 BULLET but no U+00B7, so the firmware separates with a
# bullet — screens.cpp:74. Mirrored here so the spacing reads identically.
SEP = "  •  "


# --- Font plumbing ----------------------------------------------------------------------------
def find_font(explicit: str | None) -> str:
    """Locate the exact TTF LVGL builds lv_font_montserrat_* from."""
    if explicit:
        return explicit
    if os.environ.get("JUKEBOX_MOCKUP_FONT"):
        return os.environ["JUKEBOX_MOCKUP_FONT"]

    roots = [os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))]
    # In a git worktree the built .pio tree lives in the main checkout, not here.
    try:
        common = subprocess.check_output(
            ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
            cwd=roots[0], text=True, stderr=subprocess.DEVNULL).strip()
        roots.append(os.path.dirname(common))
    except Exception:
        pass

    for root in roots:
        hits = glob.glob(os.path.join(
            root, ".pio", "libdeps", "*", "lvgl", "scripts", "built_in_font",
            "Montserrat-Medium.ttf"))
        if hits:
            return sorted(hits)[0]

    sys.exit("Montserrat-Medium.ttf not found. Build any PlatformIO env once (it ships inside "
             "LVGL), or pass --font /path/to/Montserrat-Medium.ttf")


class Fonts:
    """LVGL sizes it in px of em, and so does Pillow — lv_font_montserrat_22 == size 22."""

    def __init__(self, path: str):
        self._path = path
        self._cache: dict[int, ImageFont.FreeTypeFont] = {}

    def __call__(self, size: int) -> ImageFont.FreeTypeFont:
        if size not in self._cache:
            self._cache[size] = ImageFont.truetype(self._path, size)
        return self._cache[size]


# --- Drawing helpers --------------------------------------------------------------------------
# LVGL positions a label by the top-left of its LINE BOX; Pillow's "la" anchor is the ascender
# top, which is the closest equivalent. Everything centred in a button uses "mm", matching
# lv_obj_center().
def text(d: ImageDraw.ImageDraw, xy, s: str, font, fill, anchor="la"):
    d.text(xy, s, font=font, fill=fill, anchor=anchor)


def panel(d: ImageDraw.ImageDraw, x, y, w, h, fill, radius=0, outline=None, width=1):
    if radius:
        d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=radius,
                            fill=fill, outline=outline, width=width)
    else:
        d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=outline, width=width)


def bar(d: ImageDraw.ImageDraw, x, y, w, h, frac, track=ELEV2, fill=ACCENT):
    """The scrubber/volume primitive: 6 px track, radius 3, accent fill — screens.cpp:432."""
    r = h // 2
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=track)
    fw = int(w * max(0.0, min(1.0, frac)))
    if fw > h:
        d.rounded_rectangle([x, y, x + fw - 1, y + h - 1], radius=r, fill=fill)


# --- Icons (vector stand-ins for the LVGL glyph fonts; see module docstring) -------------------
def ic_play(d, cx, cy, s, fill):
    h = s * 0.5
    d.polygon([(cx - h * 0.42, cy - h), (cx - h * 0.42, cy + h), (cx + h * 0.78, cy)], fill=fill)


def ic_pause(d, cx, cy, s, fill):
    h, w, g = s * 0.5, s * 0.17, s * 0.15
    d.rounded_rectangle([cx - g - w, cy - h, cx - g, cy + h], radius=w * 0.3, fill=fill)
    d.rounded_rectangle([cx + g, cy - h, cx + g + w, cy + h], radius=w * 0.3, fill=fill)


def ic_prev(d, cx, cy, s, fill):
    h = s * 0.44
    d.polygon([(cx + h * 0.8, cy - h), (cx + h * 0.8, cy + h), (cx - h * 0.25, cy)], fill=fill)
    d.rectangle([cx - h * 0.75, cy - h, cx - h * 0.45, cy + h], fill=fill)


def ic_next(d, cx, cy, s, fill):
    h = s * 0.44
    d.polygon([(cx - h * 0.8, cy - h), (cx - h * 0.8, cy + h), (cx + h * 0.25, cy)], fill=fill)
    d.rectangle([cx + h * 0.45, cy - h, cx + h * 0.75, cy + h], fill=fill)


def ic_note(d, cx, cy, s, fill):
    """LV_SYMBOL_AUDIO — a beamed note."""
    w = s * 0.06
    d.rectangle([cx - s * 0.02, cy - s * 0.42, cx - s * 0.02 + w, cy + s * 0.26], fill=fill)
    d.rectangle([cx + s * 0.30, cy - s * 0.50, cx + s * 0.30 + w, cy + s * 0.16], fill=fill)
    d.polygon([(cx - s * 0.02, cy - s * 0.42), (cx + s * 0.36, cy - s * 0.50),
               (cx + s * 0.36, cy - s * 0.34), (cx - s * 0.02, cy - s * 0.26)], fill=fill)
    d.ellipse([cx - s * 0.24, cy + s * 0.12, cx + s * 0.04, cy + s * 0.34], fill=fill)
    d.ellipse([cx + s * 0.08, cy + s * 0.02, cx + s * 0.36, cy + s * 0.24], fill=fill)


def ic_heart(d, cx, cy, s, fill):
    r = s * 0.26
    d.ellipse([cx - r * 1.9, cy - r * 1.35, cx - r * 0.1, cy + r * 0.45], fill=fill)
    d.ellipse([cx + r * 0.1, cy - r * 1.35, cx + r * 1.9, cy + r * 0.45], fill=fill)
    d.polygon([(cx - r * 1.85, cy - r * 0.25), (cx + r * 1.85, cy - r * 0.25),
               (cx, cy + r * 1.7)], fill=fill)


def ic_radio(d, cx, cy, s, fill):
    w = max(2, int(s * 0.08))
    # Antenna first, at a clear diagonal — at 28 px a shallow one reads as a smudge.
    d.line([(cx - s * 0.24, cy - s * 0.20), (cx + s * 0.34, cy - s * 0.50)], fill=fill, width=w)
    d.rounded_rectangle([cx - s * 0.46, cy - s * 0.22, cx + s * 0.46, cy + s * 0.44],
                        radius=s * 0.10, outline=fill, width=w)
    r = s * 0.15                                          # tuning dial
    d.ellipse([cx + s * 0.10 - r, cy + s * 0.11 - r, cx + s * 0.10 + r, cy + s * 0.11 + r],
              outline=fill, width=w)
    for dy in (-s * 0.02, s * 0.16):                      # speaker grille
        d.line([(cx - s * 0.32, cy + dy), (cx - s * 0.14, cy + dy)], fill=fill, width=w)


def ic_speaker(d, cx, cy, s, fill):
    w2, h2 = s * 0.33, s * 0.48
    d.rounded_rectangle([cx - w2, cy - h2, cx + w2, cy + h2],
                        radius=s * 0.12, outline=fill, width=max(2, int(s * 0.08)))
    d.ellipse([cx - s * 0.19, cy + s * 0.0, cx + s * 0.19, cy + s * 0.38],
              outline=fill, width=max(2, int(s * 0.08)))
    d.ellipse([cx - s * 0.06, cy - s * 0.30, cx + s * 0.06, cy - s * 0.18], fill=fill)


def ic_gear(d, cx, cy, s, fill):
    ro, ri = s * 0.46, s * 0.30
    for i in range(8):
        a = math.radians(i * 45)
        d.line([(cx + math.cos(a) * ri * 0.9, cy + math.sin(a) * ri * 0.9),
                (cx + math.cos(a) * ro, cy + math.sin(a) * ro)],
               fill=fill, width=max(2, int(s * 0.14)))
    d.ellipse([cx - ri, cy - ri, cx + ri, cy + ri], outline=fill, width=max(2, int(s * 0.11)))
    d.ellipse([cx - s * 0.12, cy - s * 0.12, cx + s * 0.12, cy + s * 0.12], fill=fill)


def ic_wifi(d, cx, cy, s, fill):
    base = cy + s * 0.30                       # all three arcs are concentric about the dot
    for r in (s * 0.54, s * 0.37, s * 0.20):
        d.arc([cx - r, base - r, cx + r, base + r], 205, 335,
              fill=fill, width=max(2, int(s * 0.10)))
    d.ellipse([cx - s * 0.06, base - s * 0.06, cx + s * 0.06, base + s * 0.06], fill=fill)


def ic_check(d, cx, cy, s, fill):
    w = max(2, int(s * 0.16))
    d.line([(cx - s * 0.30, cy + s * 0.02), (cx - s * 0.08, cy + s * 0.24)], fill=fill, width=w)
    d.line([(cx - s * 0.08, cy + s * 0.24), (cx + s * 0.32, cy - s * 0.26)], fill=fill, width=w)


def ic_plus(d, cx, cy, s, fill):
    w, h = max(2, int(s * 0.13)), s * 0.34
    d.rectangle([cx - h, cy - w / 2, cx + h, cy + w / 2], fill=fill)
    d.rectangle([cx - w / 2, cy - h, cx + w / 2, cy + h], fill=fill)


def ic_minus(d, cx, cy, s, fill):
    w, h = max(2, int(s * 0.13)), s * 0.34
    d.rectangle([cx - h, cy - w / 2, cx + h, cy + w / 2], fill=fill)


def ic_volume(d, cx, cy, s, fill):
    d.polygon([(cx - s * 0.42, cy - s * 0.14), (cx - s * 0.22, cy - s * 0.14),
               (cx - s * 0.02, cy - s * 0.36), (cx - s * 0.02, cy + s * 0.36),
               (cx - s * 0.22, cy + s * 0.14), (cx - s * 0.42, cy + s * 0.14)], fill=fill)
    for r in (s * 0.20, s * 0.34):
        d.arc([cx - s * 0.02 - r, cy - r, cx - s * 0.02 + r, cy + r], 300, 60,
              fill=fill, width=max(2, int(s * 0.10)))


def ic_left(d, cx, cy, s, fill):
    w = max(2, int(s * 0.13))
    d.line([(cx + s * 0.16, cy - s * 0.30), (cx - s * 0.14, cy)], fill=fill, width=w)
    d.line([(cx - s * 0.14, cy), (cx + s * 0.16, cy + s * 0.30)], fill=fill, width=w)


def ic_list(d, cx, cy, s, fill):
    w = max(2, int(s * 0.11))
    for dy in (-s * 0.26, 0, s * 0.26):
        d.line([(cx - s * 0.30, cy + dy), (cx + s * 0.30, cy + dy)], fill=fill, width=w)


RAIL_ICONS = (ic_note, ic_heart, ic_radio, ic_speaker, ic_gear)


# --- Placeholder album art --------------------------------------------------------------------
def _cover(size: int, variant: int = 0, bands: bool = True) -> Image.Image:
    """A warm abstract tile. Real covers are copyrighted; the design system asks placeholder art
    to skew warm/earthy so it sits under the amber accent (readme.md, "Imagery vibe").

    `variant` shifts hue and composition so a list of stations does not look like one image
    repeated — which is what a list of real Prime Stations looks like. `bands` draws the
    reflection stripes: they read as artwork at tile size, but the screensaver blows one
    cover up to full screen, where a 4 px stripe becomes a 15 px bar across the glass.
    """
    # Sky top / sky bottom / disc, per variant. All stay in the warm half of the wheel except
    # one cool outlier, exactly as a real mixed catalogue would.
    palettes = [
        ((34, 20, 30), (150, 74, 48), (232, 137, 43)),      # dusk amber
        ((18, 26, 34), (58, 108, 116), (120, 198, 186)),    # cold teal
        ((38, 16, 22), (162, 58, 62), (240, 96, 90)),       # coral
        ((30, 22, 14), (140, 104, 44), (238, 190, 88)),     # ochre
        ((24, 18, 34), (104, 62, 128), (206, 128, 210)),    # violet
    ]
    top, bot, disc = palettes[variant % len(palettes)]
    horizon = 0.62 + 0.06 * ((variant % 3) - 1)

    # Drawn 3x oversized and shrunk, so the disc edge is antialiased. It matters most in the
    # screensaver, which blows one cover up to full screen: a hard-edged 280 px ellipse
    # stair-steps visibly at 1024, where a real decoded JPEG would not.
    out_size, size = size, size * 3
    cx = size * (0.34 + 0.16 * (variant % 4))

    img = Image.new("RGB", (size, size), "#14100f")
    d = ImageDraw.Draw(img, "RGBA")
    for y in range(size):                                     # sky gradient down to the horizon
        t = min(1.0, y / (size * horizon))
        d.line([(0, y), (size, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bot)))

    cy, r0 = size * horizon, size * 0.20                      # sun/moon, partly below the horizon
    for r in range(int(r0), 0, -1):
        t = r / r0
        d.ellipse([cx - r, cy - r, cx + r, cy + r],
                  fill=tuple(int(c - 26 * t) for c in disc))

    # Ground: a gradient rather than a flat slab, so the horizon does not become a hard bar when
    # the screensaver upscales a <=280 px cover to fill 1024x600.
    for y in range(int(cy), size):
        t = (y - cy) / max(1.0, size - cy)
        d.line([(0, y), (size, y)],
               fill=tuple(int(c * (0.22 - 0.16 * t)) for c in bot))
    if bands:
        for i in range(4):                                    # reflection bands, fading down
            yy = cy + size * (0.05 + i * 0.085)
            d.rectangle([0, yy, size, yy + size * 0.014], fill=(*disc, 96 - i * 22))
    img = img.resize((out_size, out_size), Image.Resampling.LANCZOS)
    ImageDraw.Draw(img, "RGBA").rectangle([0, 0, out_size - 1, out_size - 1],
                                          outline=(255, 255, 255, 18))
    return img


# ==============================================================================================
# Shared chrome
# ==============================================================================================
def draw_rail(d: ImageDraw.ImageDraw, F: Fonts, active: int):
    """screens.cpp:337 — 96 px rail, 72 px items on an 86 px pitch, hairline divider."""
    panel(d, 0, 0, RAIL_W, SCREEN_H, BG)
    panel(d, RAIL_W, 0, 1, SCREEN_H, LINE)
    for i in range(5):
        x = (RAIL_W - RAIL_BTN) // 2
        y = PAD_TOP + i * RAIL_STEP
        if i == active:
            panel(d, x, y, RAIL_BTN, RAIL_BTN, ELEV2, radius=R_LG)
        RAIL_ICONS[i](d, x + RAIL_BTN / 2, y + RAIL_BTN / 2, 28,
                      ACCENT if i == active else DIM)


def draw_status(d: ImageDraw.ImageDraw, F: Fonts, room: str, group: str, playing: bool):
    """screens.cpp:368 — dot, room, group line, Wi-Fi glyph. Dot is accent while playing."""
    x = CONTENT_X
    d.ellipse([x, PAD_TOP + 6, x + 8, PAD_TOP + 14], fill=ACCENT if playing else DIM)
    text(d, (x + 18, PAD_TOP), room, F(16), TEXT)
    if group:
        text(d, (x + 18, PAD_TOP + 20), group, F(12), DIM)
    ic_wifi(d, CONTENT_X + CONTENT_W - 12, PAD_TOP + 10, 20, MUTED)


def new_screen() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (SCREEN_W, SCREEN_H), BG)
    return img, ImageDraw.Draw(img)


# ==============================================================================================
# Pages
# ==============================================================================================
def render_now_playing(F: Fonts) -> Image.Image:
    img, d = new_screen()
    draw_rail(d, F, 0)
    draw_status(d, F, "Living Room", "Grouped with Kitchen" + SEP + "Dining Room", True)

    x = CONTENT_X
    # Album art tile — rounded, hairline, real cover pasted through a rounded mask.
    cov = _cover(ART)
    mask = Image.new("L", (ART, ART), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, ART - 1, ART - 1], radius=R_LG, fill=255)
    img.paste(cov, (x, NP_ART_TOP), mask)
    d.rounded_rectangle([x, NP_ART_TOP, x + ART - 1, NP_ART_BOT - 1],
                        radius=R_LG, outline=LINE, width=1)

    tx = x + ART + GAP
    tw = CONTENT_W - ART - GAP

    text(d, (tx, NP_BADGE_Y), "PLAYING", F(12), ACCENT)
    # Two reserved title lines; a one-line title is re-centred in the slot (placeTitle()).
    text(d, (tx, NP_TITLE_Y + (NP_TITLE_H - NP_TITLE_LH) // 2), "Night Ferry", F(48), TEXT)
    text(d, (tx, NP_META_Y), "Aurora Field" + SEP + "Blue Hour", F(22), MUTED)

    bar(d, tx, NP_TRACK_Y, tw, 6, 102 / 240)
    text(d, (tx, NP_TIMES_Y), "1:42", F(12), DIM)
    text(d, (tx + tw, NP_TIMES_Y), "-2:18", F(12), DIM, anchor="ra")

    # Bottom row — volume left, transport right (screens.cpp:444).
    row_y = SCREEN_H - PAD_BOT
    ic_volume(d, x + 10, row_y - 12 - 10, 20, MUTED)
    bar(d, x + 34, row_y - 18 - 6, 260, 6, 0.34)
    text(d, (x + 306, row_y - 14 - 12), "34", F(12), MUTED)

    prev_c = (CONTENT_X + CONTENT_W - (72 + 56 + 28) - 56 // 2, row_y - 56 // 2)
    play_c = (CONTENT_X + CONTENT_W - (56 + 14) - 72 // 2, row_y - 72 // 2)
    next_c = (CONTENT_X + CONTENT_W - 56 // 2, row_y - 56 // 2)
    d.ellipse([prev_c[0] - 28, prev_c[1] - 28, prev_c[0] + 28, prev_c[1] + 28], fill=ELEV2)
    ic_prev(d, *prev_c, 22, TEXT)
    d.ellipse([play_c[0] - 36, play_c[1] - 36, play_c[0] + 36, play_c[1] + 36], fill=ACCENT)
    ic_pause(d, *play_c, 26, ACCENT_INK)          # playing -> pause glyph
    d.ellipse([next_c[0] - 28, next_c[1] - 28, next_c[0] + 28, next_c[1] + 28], fill=ELEV2)
    ic_next(d, *next_c, 22, TEXT)
    return img


ROOMS = [
    # name,           in_group, active, playing, vol,  coordinator
    ("Living Room",   True,  True,  True,  34, True),
    ("Kitchen",       True,  False, True,  28, False),
    ("Dining Room",   True,  False, True,  31, False),
    ("Bedroom",       False, False, False, 18, True),
    ("Office",        False, False, False, 22, True),
    ("Bathroom",      False, False, False, 12, True),
]


def render_rooms(F: Fonts) -> Image.Image:
    img, d = new_screen()
    draw_rail(d, F, 3)
    draw_status(d, F, "Living Room", "Grouped with Kitchen" + SEP + "Dining Room", True)

    x = CONTENT_X
    members = [r for r in ROOMS if r[1]]
    gvol = sum(r[4] for r in members) // len(members)

    # --- Group summary bar (screens.cpp:962) ---
    panel(d, x, RM_BAR_Y, RM_W, RM_BAR_H, ELEV, radius=R_LG, outline=LINE)
    text(d, (x + 18, RM_BAR_Y + 14), f"{len(members)} rooms", F(24), TEXT)
    text(d, (x + 18, RM_BAR_Y + 48), SEP.join(r[0] for r in members), F(12), DIM)
    text(d, (x + 300, RM_BAR_Y + 18), "GROUP VOLUME", F(12), DIM)
    bar(d, x + 300, RM_BAR_Y + 46, 320, 6, gvol / 100)
    text(d, (x + 628, RM_BAR_Y + 42), str(gvol), F(12), DIM)

    ug_x = x + RM_W - (16 + R_PLAY_SZ + 10) - 120
    ug_y = RM_BAR_Y + (RM_BAR_H - 44) // 2
    panel(d, ug_x, ug_y, 120, 44, ELEV2, radius=10, outline=LINE)
    text(d, (ug_x + 60, ug_y + 22), "UNGROUP", F(12), TEXT, anchor="mm")

    gp_c = (x + RM_W - 16 - R_PLAY_SZ // 2, RM_BAR_Y + RM_BAR_H // 2)
    d.ellipse([gp_c[0] - 24, gp_c[1] - 24, gp_c[0] + 24, gp_c[1] + 24], fill=ACCENT)
    ic_pause(d, *gp_c, 20, ACCENT_INK)

    text(d, (x + 2, RM_LIST_Y - 24), "ALL ROOMS" + SEP + "TAP THE BOX TO GROUP", F(12), DIM)

    # --- Room rows (screens.cpp:756) ---
    for i, (name, in_grp, active, playing, vol, coord) in enumerate(ROOMS):
        ry = RM_LIST_Y + i * RM_ROW_PITCH
        mid = ry + RM_ROW_H // 2
        border = ACCENT if active else (LINE if in_grp else BG)
        panel(d, x, ry, RM_W, RM_ROW_H, ELEV if in_grp else BG, radius=R_MD,
              outline=border, width=1)

        # Checkbox: accent fill + tick in-group, hollow outline out. The anchor's own box is
        # inert and drawn at 40 % — it cannot leave its own group (screens.cpp:884).
        cx0, cy0 = x + RX_CHECK, mid - RC_SZ // 2
        if in_grp:
            fill = "#5f3a17" if active else ACCENT      # LV_OPA_40 over ELEV, pre-computed
            d.rounded_rectangle([cx0, cy0, cx0 + RC_SZ, cy0 + RC_SZ], radius=8, fill=fill)
            ic_check(d, cx0 + RC_SZ / 2, cy0 + RC_SZ / 2, 26, ACCENT_INK if not active else "#d8bfa4")
        else:
            d.rounded_rectangle([cx0, cy0, cx0 + RC_SZ, cy0 + RC_SZ], radius=8,
                                outline=DIM, width=2)

        text(d, (x + RX_NAME, mid - 10 - 8), name, F(16), TEXT if in_grp else MUTED)
        if active:
            text(d, (x + RX_BADGE, mid - 10 - 6), "MAIN", F(12), ACCENT)
        if not playing:
            cap = "Idle"
        elif in_grp and len(members) > 1:
            cap = "Playing" + SEP + "grouped"
        else:
            cap = "Playing"
        text(d, (x + RX_NAME, mid + 12 - 6), cap, F(12), DIM)

        # Volume; dimmed to 50 % when the room is idle, per the design's opacity .45.
        bar(d, x + RX_VOL, mid - 3, RV_W, 6, vol / 100,
            fill=ACCENT if playing else "#7a4a1d")
        text(d, (x + RX_VPCT, mid - 6), str(vol), F(12), DIM)

        for bx, ic in ((RX_MINUS, ic_minus), (RX_PLUS, ic_plus)):
            panel(d, x + bx, mid - R_STEP_SZ // 2, R_STEP_SZ, R_STEP_SZ, ELEV2,
                  radius=10, outline=LINE)
            ic(d, x + bx + R_STEP_SZ / 2, mid, 20, MUTED)

        # Play/pause only where it is honest — coordinators and solo rooms (screens.cpp:919).
        if coord or not in_grp:
            pc = (x + RX_PLAY + R_PLAY_SZ / 2, mid)
            d.ellipse([pc[0] - 24, pc[1] - 24, pc[0] + 24, pc[1] + 24],
                      fill=ACCENT if playing else ELEV2)
            (ic_pause if playing else ic_play)(d, *pc, 20, ACCENT_INK if playing else MUTED)
    return img


STATIONS = [
    "Amber Hours Radio",
    "Blue Ridge Bluegrass",
    "Cassette Summer",
    "Deep Field Ambient",
    "Evening Standards",
]


def render_radio(F: Fonts) -> Image.Image:
    """The stations level of Radio: back arrow, genre title, 96 px rows, A-Z jump strip."""
    img, d = new_screen()
    draw_rail(d, F, 2)
    draw_status(d, F, "Living Room", "Grouped with Kitchen" + SEP + "Dining Room", True)

    x = CONTENT_X
    ic_left(d, x + 26, PAD_TOP + 44 + 26, 30, TEXT)
    text(d, (x + 62, PAD_TOP + 52), "Chill & Ambient", F(28), TEXT)

    sb_x, sb_y = x + CONTENT_W - 56, PAD_TOP + 44
    panel(d, sb_x, sb_y, 56, 52, ELEV, radius=R_MD)
    ic_list(d, sb_x + 28, sb_y + 26, 22, MUTED)

    # The list is a scroll container, so rows are drawn into a layer the size of its viewport and
    # pasted — the row straddling the bottom edge then truncates the way it does on the panel,
    # rather than being skipped and leaving a hole.
    top = PAD_TOP + 106
    row_h, row_gap = 96, 10
    list_h = SCREEN_H - top - (PAD_BOT + 54)        # radioLayout(withAz=True), screens.cpp:1477
    layer = Image.new("RGB", (CONTENT_W, list_h), BG)
    ld = ImageDraw.Draw(layer)
    for i, name in enumerate(STATIONS):
        ry = i * (row_h + row_gap)
        if ry >= list_h:
            break
        ld.rounded_rectangle([0, ry, CONTENT_W - 1, ry + row_h - 1], radius=R_LG, fill=ELEV)
        tile = _cover(72, variant=i)
        m = Image.new("L", (72, 72), 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, 71, 71], radius=R_MD, fill=255)
        layer.paste(tile, (12, ry + (row_h - 72) // 2), m)
        text(ld, (100, ry + row_h // 2 - 12 - 11), name, F(22), TEXT)
        text(ld, (100, ry + row_h // 2 + 14 - 6), "Prime Station", F(12), DIM)
    img.paste(layer, (x, top))

    # A-Z strip: 26 targets across the content width; letters with no stations dimmed but kept,
    # so the strip has a stable shape a thumb can learn (screens.cpp:1614).
    step = CONTENT_W // 26
    have = {ord(c) - 65 for c in "ABCDEHJLMNORSTVW"}
    ay = SCREEN_H - PAD_BOT - 44
    for i in range(26):
        text(d, (x + i * step + step // 2, ay + 22), chr(65 + i), F(16),
             MUTED if i in have else LINE, anchor="mm")
    return img


def render_screensaver(F: Fonts, font_path: str) -> Image.Image:
    """Cover layout (plans/10): art washed full-bleed under a 50 % scrim, clock + date + track."""
    img = Image.new("RGB", (SCREEN_W, SCREEN_H), BG)
    # LV_IMAGE_ALIGN_COVER fills the widget keeping aspect, so a square <=280 px cover becomes
    # 1024x1024 and is centre-cropped. Bilinear, not Lanczos: that is the softness LVGL
    # actually produces, and the comment in saverApplyArt() says the wash is meant to read as
    # a wash rather than a sharpened thumbnail.
    wash = _cover(280, bands=False).resize((SCREEN_W, SCREEN_W), Image.Resampling.BILINEAR)
    img.paste(wash, (0, (SCREEN_H - SCREEN_W) // 2))
    img = Image.blend(img, Image.new("RGB", img.size, "#000000"), 0.5)
    d = ImageDraw.Draw(img)

    gx, gy = (SCREEN_W - SAVER_W) // 2, (SCREEN_H - SAVER_H) // 2
    clock = ImageFont.truetype(font_path, 120)   # lv_font_clock_120 — a real font, not a scale
    # The firmware's rhythm comment budgets the time label at 86 px tall and puts the date at
    # +98 (screens.cpp:2277). 86 is the DIGIT height, not the em box — Pillow's ascender anchor
    # would drop the digits ~30 px and land them on the date, so align the ink box instead.
    cxm = gx + SAVER_W // 2
    bbox = clock.getbbox("10:42")
    d.text((cxm, gy - bbox[1]), "10:42", font=clock, fill=TEXT, anchor="ma")
    # AM/PM is hung off the time's right edge, near its baseline (saverPaintClock()).
    w = d.textlength("10:42", font=clock)
    text(d, (cxm + w / 2 + 12, gy + 86 - 26), "PM", F(28), MUTED)
    text(d, (gx + SAVER_W // 2, gy + 98), "Tuesday, August 24", F(22), MUTED, anchor="ma")
    text(d, (gx + SAVER_W // 2, gy + 150), "Night Ferry", F(28), TEXT, anchor="ma")
    text(d, (gx + SAVER_W // 2, gy + 190), "Aurora Field" + SEP + "Blue Hour", F(16), DIM,
         anchor="ma")
    return img


# ==============================================================================================
def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "images"))
    ap.add_argument("--font", default=None)
    args = ap.parse_args()

    font_path = find_font(args.font)
    F = Fonts(font_path)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    screens = {
        "jukebox-now-playing.png": render_now_playing(F),
        "jukebox-rooms.png":       render_rooms(F),
        "jukebox-radio.png":       render_radio(F),
        "jukebox-screensaver.png": render_screensaver(F, font_path),
    }
    for name, im in screens.items():
        path = os.path.join(out, name)
        im.save(path, optimize=True)
        print(f"{path}  {im.width}x{im.height}")
    print(f"font: {font_path}")


if __name__ == "__main__":
    main()
