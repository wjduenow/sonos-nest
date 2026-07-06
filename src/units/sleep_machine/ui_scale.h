// Relative sizing for the ES3C28P 240x320 portrait panel. Layout is expressed as
// fractions of screen size so the UX stays resolution-independent. (Mirrors the nest's
// ui_scale.h with this unit's dimensions.)
#pragma once

#define SCREEN_W   240
#define SCREEN_H   320

// Fraction-of-screen helpers (lv_coord_t is in scope wherever these expand).
#define SW(pct)  ((lv_coord_t)(SCREEN_W * (pct) / 100))
#define SH(pct)  ((lv_coord_t)(SCREEN_H * (pct) / 100))
