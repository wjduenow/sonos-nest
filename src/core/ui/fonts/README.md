# Latin-1 supplement fonts

`lv_font_mont_latin_<px>.c` — Montserrat at one size, holding **only U+00A0–U+00FF**, generated
with `lv_font_conv` from the exact `Montserrat-Medium.ttf` that LVGL builds its own faces from.
Each file's header carries the command that produced it; regenerate rather than edit.

## What they are for

LVGL's built-in `lv_font_montserrat_*` carry ASCII only (0x20–0x7F plus 0xB0 and 0x2022) alongside
a FontAwesome subset for `LV_SYMBOL_*`. Every accented Latin character is absent, so LVGL draws its
missing-glyph box instead — Sonos metadata rendered "Mötley Crüe" as "M□tley Cr□e" (issue #21).

None of these is used directly. Each unit that shows metadata has a `latin_fonts.{h,cpp}` that
copies the built-in into a writable `lv_font_t` and points its `.fallback` at the file here of the
same size. `lv_font_get_glyph_dsc()` walks the fallback chain, so ASCII and `LV_SYMBOL_*` still
resolve out of the built-in and only the accented characters come from here.

**The direction is load-bearing, twice over.** Issue #21 proposed full replacement fonts and
dismissed `lv_font_t::fallback` because the built-ins are `const` — but the field that needs
setting is the wrapper's, not the built-in's. Wrapping is roughly half the flash of a replacement,
and it cannot break `LV_SYMBOL_*`: a replacement generated from `Montserrat-Medium.ttf` alone would
carry no FontAwesome glyphs, and the units draw most of their icons out of the montserrat faces.
Wrapping also pins `line_height`/`base_line`, which LVGL takes from the **top** font only — the
supplements' own are taller (at 22 px `lv_font_conv` reports 28/6 against the built-in's 24/4,
because accents sit above cap height and the cedilla hangs below the descender), so a supplement
used as the top font would silently grow every content label and invalidate each unit's hardcoded
layout constants.

## Why they live here and not in a unit

More than one unit needs the same size (28 px is used by all three screened units; 20 and 24 by
both S3 units), and these are large generated files — the 48 px source is 277 KB. `+<core/>` sweeps
them into every screened env and `-<core/ui/>` drops the whole subtree from the headless button
envs, which is the same rule the rest of `core/ui/` follows (see `../README.md`).

**A size a unit does not reference costs it nothing in flash.** Verified on hardware-bound builds,
2026-09-07: with `lv_font_mont_latin_20.c` and `_24.c` present but referenced by nothing, both
compiled into `.pio/build/sleep-machine/` and **neither symbol appears in `firmware.elf`** — the
build is `-ffunction-sections -fdata-sections` and links `--gc-sections`, so unreferenced font data
is dropped. The cost of an unused size here is compile time, not image size. That is what makes one
shared directory preferable to a copy per unit.

## Sizes, and who references them

| px | jukebox | nest | sleep-machine |
|---|---|---|---|
| 12 | yes | – | – |
| 16 | yes | – | – |
| 20 | – | yes | yes |
| 22 | yes | – | – |
| 24 | – | yes | yes |
| 28 | yes | yes | yes |
| 48 | yes | – | – |

The 48 px face is by far the largest (45 KB linked, against 4.3 KB for 12 px), so add one only for
a size that actually renders text off the network. Sizes that draw only `LV_SYMBOL_*` glyphs,
numbers or our own English chrome do not need one — both S3 units' 48 px labels are clock digits
and an icon, which is why neither references it.
