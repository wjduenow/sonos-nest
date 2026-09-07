#!/usr/bin/env python3
"""Generate the stand-in tiles the README shows until real device photos exist.

These are DELIBERATELY not renders of the hardware. `hardware/*/render_preview.png` already
exists and is an engineering drawing — exploded views, dimension callouts, section cuts. It
belongs in the build docs, and putting it at the top of a product README would misrepresent
what these devices look like.

So each tile just says which photo is missing, in the product's own visual language, at the
aspect ratio the README lays out. To replace one, overwrite the PNG at the same path with a
real photo — the README needs no edit. Framing and aspect for each shot: docs/images/README.md.

Usage:
    python3 docs/mockups/render_photo_placeholders.py [--out docs/images] [--font <ttf>]

Skips any file that is not a placeholder (checked via a PNG text chunk), so re-running it
after photos have landed will not overwrite them.
"""

from __future__ import annotations

import argparse
import os
import sys

from PIL import Image, ImageDraw, PngImagePlugin

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_jukebox_screens import (ACCENT, BG, DIM, ELEV, LINE, MUTED, TEXT,  # noqa: E402
                                    Fonts, find_font)

MARKER = "sonos-nest-photo-placeholder"

# name -> (width, height, headline, what the shot has to show)
SHOTS = {
    "hero":               (2000, 900, "The family",
                           "all five units together"),
    "unit-nest":          (1200, 900, "sonos-nest",
                           "the round knob, on the wall"),
    "unit-sleep-machine": (1200, 900, "sonos-sleep-machine",
                           "on a nightstand, screen lit"),
    "unit-button":        (1200, 900, "sonos-button",
                           "in the hand, ring lit"),
    "unit-button-v2":     (1200, 900, "sonos-button-v2",
                           "beside the v1, for scale"),
    "unit-jukebox":       (1200, 900, "sonos-jukebox",
                           "the 7-inch panel, wall-mounted"),
}


def is_placeholder(path: str) -> bool:
    if not os.path.exists(path):
        return True
    try:
        with Image.open(path) as im:
            return im.info.get("Comment") == MARKER
    except Exception:
        return False


def tile(w: int, h: int, headline: str, subject: str, F: Fonts) -> Image.Image:
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img, "RGBA")

    # A single soft amber sweep across the lower third — enough to read as "designed", not
    # enough to be mistaken for a picture of something.
    for i in range(h // 3):
        t = i / (h // 3)
        d.line([(0, h - i), (w, h - i)],
               fill=(232, 137, 43, int(16 * (1 - t))))
    d.rectangle([0, 0, w - 1, h - 1], outline=LINE, width=2)

    big = F(max(28, int(h * 0.075)))
    small = F(max(14, int(h * 0.030)))
    tiny = F(max(12, int(h * 0.024)))

    cx, cy = w // 2, h // 2
    d.text((cx, cy - h * 0.06), headline, font=big, fill=TEXT, anchor="mm")
    d.text((cx, cy + h * 0.045), subject, font=small, fill=MUTED, anchor="mm")

    # Corner ticks — a framing mark, so the tile reads as a reserved slot.
    arm, off = int(h * 0.055), int(h * 0.045)
    for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
        x = off if sx > 0 else w - off
        y = off if sy > 0 else h - off
        d.line([(x, y), (x + sx * arm, y)], fill=ACCENT, width=3)
        d.line([(x, y), (x, y + sy * arm)], fill=ACCENT, width=3)

    label = "PHOTO PENDING  •  docs/images/README.md"
    tw = d.textlength(label, font=tiny)
    pad_x, pad_y = int(h * 0.022), int(h * 0.016)
    bx0, by0 = cx - tw / 2 - pad_x, h - int(h * 0.115)
    d.rounded_rectangle([bx0, by0, cx + tw / 2 + pad_x, by0 + tiny.size + pad_y * 2],
                        radius=int(h * 0.016), fill=ELEV, outline=LINE)
    d.text((cx, by0 + pad_y + tiny.size / 2), label, font=tiny, fill=DIM, anchor="mm")
    return img


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "images"))
    ap.add_argument("--font", default=None)
    ap.add_argument("--force", action="store_true",
                    help="regenerate even where a real photo has replaced the placeholder")
    args = ap.parse_args()

    F = Fonts(find_font(args.font))
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    meta = PngImagePlugin.PngInfo()
    meta.add_text("Comment", MARKER)

    for name, (w, h, headline, subject) in SHOTS.items():
        path = os.path.join(out, f"{name}.png")
        if not args.force and not is_placeholder(path):
            print(f"{path}  kept (real photo)")
            continue
        tile(w, h, headline, subject, F).save(path, optimize=True, pnginfo=meta)
        print(f"{path}  {w}x{h}")


if __name__ == "__main__":
    main()
