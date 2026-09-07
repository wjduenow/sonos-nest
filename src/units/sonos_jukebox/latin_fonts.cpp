// Builds the Latin-1-capable montserrat faces. See latin_fonts.h for why this shape.
#include "latin_fonts.h"

// Generated, checked in beside this file; each holds U+00A0-U+00FF only.
LV_FONT_DECLARE(lv_font_mont_latin_12);
LV_FONT_DECLARE(lv_font_mont_latin_16);
LV_FONT_DECLARE(lv_font_mont_latin_22);
LV_FONT_DECLARE(lv_font_mont_latin_28);
LV_FONT_DECLARE(lv_font_mont_latin_48);

lv_font_t jbFont12, jbFont16, jbFont22, jbFont28, jbFont48;

namespace {

// The built-ins are const with constant initialisers, so they are fully formed during static
// initialisation — before any dynamic initialiser, this one included, can observe them.
lv_font_t withFallback(const lv_font_t &base, const lv_font_t *extra) {
  lv_font_t f = base;
  f.fallback = extra;
  return f;
}

struct Wire {
  Wire() {
    jbFont12 = withFallback(lv_font_montserrat_12, &lv_font_mont_latin_12);
    jbFont16 = withFallback(lv_font_montserrat_16, &lv_font_mont_latin_16);
    jbFont22 = withFallback(lv_font_montserrat_22, &lv_font_mont_latin_22);
    jbFont28 = withFallback(lv_font_montserrat_28, &lv_font_mont_latin_28);
    jbFont48 = withFallback(lv_font_montserrat_48, &lv_font_mont_latin_48);
  }
};
const Wire s_wire;

}  // namespace
