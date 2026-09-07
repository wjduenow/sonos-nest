// Builds the Latin-1-capable montserrat faces. See latin_fonts.h and src/core/ui/fonts/README.md.
#include "latin_fonts.h"

// Generated, in src/core/ui/fonts/; each holds U+00A0-U+00FF only. Shared with the other screened
// units — see that directory's README for why they live there rather than here.
LV_FONT_DECLARE(lv_font_mont_latin_20);
LV_FONT_DECLARE(lv_font_mont_latin_24);
LV_FONT_DECLARE(lv_font_mont_latin_28);

lv_font_t smFont20, smFont24, smFont28;

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
    smFont20 = withFallback(lv_font_montserrat_20, &lv_font_mont_latin_20);
    smFont24 = withFallback(lv_font_montserrat_24, &lv_font_mont_latin_24);
    smFont28 = withFallback(lv_font_montserrat_28, &lv_font_mont_latin_28);
  }
};
const Wire s_wire;

}  // namespace
