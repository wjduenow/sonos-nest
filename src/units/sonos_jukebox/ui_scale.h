// Relative sizing for the CrowPanel Advance 7" panel in its LANDSCAPE wall orientation
// (1024x600 native, no rotation). Layout is expressed as fractions of screen size so the same UX
// scales to the 5" 800x480 SKU the design system also specifies. (Mirrors the nest and
// sleep-machine ui_scale.h.)
#pragma once

#define SCREEN_W   1024
#define SCREEN_H   600

// Fraction-of-screen helpers (lv_coord_t is in scope wherever these expand).
#define SW(pct)  ((lv_coord_t)(SCREEN_W * (pct) / 100))
#define SH(pct)  ((lv_coord_t)(SCREEN_H * (pct) / 100))

// --- Design-system tokens (.claude/skills/sonos-jukebox-design/tokens/colors.css) -------------
// Kept as raw hex so they read identically to the CSS they came from. One accent at a time; the
// alternates are here because the design system explicitly offers them, not to be mixed in.
#define JB_SCREEN_BG        0x0e0f12   // --screen-bg
#define JB_SCREEN_ELEV      0x191b20   // --screen-elev
#define JB_SCREEN_ELEV_2    0x23262d   // --screen-elev-2
#define JB_SCREEN_LINE      0x2c3038   // --screen-line
#define JB_TEXT             0xf4f5f7   // --screen-text
#define JB_TEXT_MUTED       0x9aa0ab   // --screen-text-mut
#define JB_TEXT_DIM         0x6a7079   // --screen-text-dim
#define JB_ACCENT           0xe8892b   // --accent (amber; teal 0x2fa5a0 / coral 0xf0605a are the
                                       // documented alternates — swap this one line, not uses)
#define JB_ACCENT_INK       0xffffff   // --accent-ink

// Radii, in px, straight from tokens/radii.css.
#define JB_R_MD  14
#define JB_R_LG  20
#define JB_R_XL  28
