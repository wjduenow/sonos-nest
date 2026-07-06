// UX contract — the universal subset every unit (src/units/<unit>/) implements. The core
// app/boot layer calls only these; anything richer (screen enums, uiShow, etc.) stays
// private to the unit's own screens.h. Keep this header free of LVGL and unit internals.
#pragma once

void uiInit();    // build the unit's LVGL screens/widgets (call after boardInit())
void uiTick();    // per-frame: lv_timer_handler() + route this unit's input
