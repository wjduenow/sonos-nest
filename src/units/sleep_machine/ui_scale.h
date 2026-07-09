// Relative sizing for the ES3C28P panel in its LANDSCAPE nightstand orientation (the board
// runs at DISPLAY_ROTATION 1 -> logical 320x240). Layout is expressed as fractions of screen
// size so the UX stays resolution-independent. (Mirrors the nest's ui_scale.h.)
#pragma once

#define SCREEN_W   320
#define SCREEN_H   240

// Fraction-of-screen helpers (lv_coord_t is in scope wherever these expand).
#define SW(pct)  ((lv_coord_t)(SCREEN_W * (pct) / 100))
#define SH(pct)  ((lv_coord_t)(SCREEN_H * (pct) / 100))
