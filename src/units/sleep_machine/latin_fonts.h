#pragma once
// Montserrat with Latin-1, for the labels on this unit that can show text off the network or off
// the SD card.
//
// LVGL's built-in lv_font_montserrat_* carry ASCII only, so accented characters drew as the
// missing-glyph box: "Mötley Crüe" as "M□tley Cr□e" (issue #21). Each smFontNN below is a writable
// copy of the built-in at that size with .fallback pointing at the matching lv_font_mont_latin_NN,
// so ASCII and LV_SYMBOL_* still resolve out of the built-in and only U+00A0-U+00FF come from the
// supplement.
//
// Read src/core/ui/fonts/README.md before changing this — it has why the wrapping goes this way
// round rather than replacing the built-ins outright, and why the copy is what keeps this unit's
// layout constants valid.
//
// Three sizes, because those are the three that render text this unit did not write itself:
//   20  makeListButton() — the track picker, whose rows are SD filenames. Also the Wi-Fi SSID
//       label and the "Connecting to <ssid>" line; an SSID is arbitrary bytes off the air.
//   24  makeButton() — Sonos room names on the Rooms page and SSIDs in the scan list.
//   28  the now-playing track title (s_playTitle).
// 48 is the screensaver clock and an LV_SYMBOL_AUDIO glyph, and 14 is our own chrome, so neither
// needs one — and 48 is by far the largest face, so leaving it out is most of what keeps this
// cheap. That matters more here than on any other unit: this is the tightest build in the repo,
// idling at ~14.5 KB minimum free internal heap (see CLAUDE.md). Flash is not the constraint, but
// nothing about this should grow either.
#include <lvgl.h>

extern lv_font_t smFont20, smFont24, smFont28;
