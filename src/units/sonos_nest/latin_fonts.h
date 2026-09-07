#pragma once
// Montserrat with Latin-1, for the labels on this unit that can show text off the network.
//
// LVGL's built-in lv_font_montserrat_* carry ASCII only, so accented characters in Sonos metadata
// drew as the missing-glyph box: "Mötley Crüe" as "M□tley Cr□e" (issue #21). Each nestFontNN below
// is a writable copy of the built-in at that size with .fallback pointing at the matching
// lv_font_mont_latin_NN, so ASCII and LV_SYMBOL_* still resolve out of the built-in and only
// U+00A0-U+00FF come from the supplement.
//
// Read src/core/ui/fonts/README.md before changing this — it has why the wrapping goes this way
// round rather than replacing the built-ins outright, and why the copy is what keeps this unit's
// layout constants valid.
//
// Three sizes, because those are the three that render network text on this unit:
//   20  the room name under the art (s_zone). The other 20 px labels are our own chrome.
//   24  the artist line (s_artist).
//   28  the now-playing title (s_title) and every browse-list row (LIST_FONT).
// 48 is the clock's digits and 14 is the OTA hostname, so neither needs one — and 48 is by far the
// largest face, so leaving it out is most of what keeps this cheap.
#include <lvgl.h>

extern lv_font_t nestFont20, nestFont24, nestFont28;
