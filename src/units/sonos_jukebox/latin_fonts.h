#pragma once
// Montserrat with Latin-1, for every label on this unit that can show text off the network.
//
// WHY THIS EXISTS. LVGL's built-in lv_font_montserrat_* carry ASCII only (0x20-0x7F, plus 0xB0 and
// 0x2022) alongside a FontAwesome subset for LV_SYMBOL_*. Every accented Latin character is absent,
// so LVGL draws its missing-glyph box instead: "Mötley Crüe" rendered as "M□tley Cr□e" on Now
// Playing (issue #21). Sonos metadata is full of them — artists, albums, station names, and room
// names the owner typed themselves.
//
// HOW. Each jbFontNN below is a byte copy of the built-in at that size with .fallback pointing at
// lv_font_mont_latin_NN — a generated font holding ONLY U+00A0-U+00FF. lv_font_get_glyph_dsc()
// walks the fallback chain (lvgl/src/font/lv_font.c), so ASCII and LV_SYMBOL_* still resolve out of
// the built-in and only the accented characters come from ours.
//
// *** THE COPY IS THE POINT, AND SO IS ITS DIRECTION. *** Issue #21 dismissed lv_font_t::fallback
// because "the built-ins are const, so the field cannot be set at runtime" — true, but the field
// that needs setting is OURS, not theirs. Copying the built-in into writable storage and pointing
// it at the supplement gets the whole thing for ~96 extra glyphs per size instead of a full ~800-
// glyph replacement, and it cannot break LV_SYMBOL_*: a replacement font generated from
// Montserrat-Medium.ttf alone would have no FontAwesome glyphs, and half the icons on this panel
// are drawn out of the montserrat faces.
//
// *** THE COPY ALSO PINS THE METRICS, WHICH IS LOAD-BEARING. *** line_height and base_line come
// from the TOP font only, and the supplement's own are taller — at 22 px lv_font_conv reports
// 28/6 against the built-in's 24/4, because accents on capitals sit above ASCII's cap height and
// the cedilla hangs below its descender. Taking the built-in as the top font keeps every one of
// this unit's hardcoded layout constants (NP_TITLE_LH, NP_META_H, NP_SMALL_H in screens.cpp)
// exactly as measured. The accepted cost is that a few capitals — Å is the worst, at 2 px over the
// line box at 22 px; Ö and Ü are level with it — can clip by a pixel or two in a label sized to
// its content. Lowercase, which is what "Mötley Crüe" and almost all real metadata is, never does.
//
// Statically initialised via a file-scope constructor in latin_fonts.cpp rather than an init call
// from uiInit(), deliberately: there is no call for a future edit to forget, and no window in which
// one of these is a zeroed struct with a null get_glyph_dsc.
#include <lvgl.h>

// *** AUDIT BY WHAT A LABEL IS LATER SET TO, NOT BY THE TEXT IT IS CREATED WITH. *** 24 px was
// first left out of this list on the strength of its call sites — every one of them creates an
// LV_SYMBOL_* or a number. s_grpCount is created as "--" and then set to s_roomsData[a].name
// whenever the group has a single member, so an accented room name still drew boxes in the
// Rooms summary bar. Caught in review on PR #25, not by reading the constructors.
//
// 20 px is deliberately NOT here, and was re-checked the same way: all seven of its call sites
// create an LV_SYMBOL_*, and all four later lv_label_set_text() calls write one too.
extern lv_font_t jbFont12;   // badges, timecodes, browse-row subtitles, group member lists
extern lv_font_t jbFont16;   // status-bar room name, room rows, dropdowns, screensaver meta
extern lv_font_t jbFont22;   // Now Playing artist/album, browse row titles, favourites
extern lv_font_t jbFont24;   // the Rooms group summary, which shows a lone room's NAME
extern lv_font_t jbFont28;   // page titles (a browsed genre name), screensaver track title
extern lv_font_t jbFont48;   // Now Playing title
