// Relative sizing helpers for the 480x480 round panel (pattern borrowed from SonosESP).
// Express layout as fractions of screen size so the UI stays resolution-independent.
#pragma once

#define SCREEN_W   480
#define SCREEN_H   480

// Fraction-of-screen helpers.
#define SW(pct)  ((lv_coord_t)(SCREEN_W * (pct) / 100))
#define SH(pct)  ((lv_coord_t)(SCREEN_H * (pct) / 100))
