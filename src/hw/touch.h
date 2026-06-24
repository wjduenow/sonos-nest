// Capacitive touch (GT911-class). Reset/INT routed via PCF8574. See plan §2.
#pragma once

void touchInit();
// LVGL input read callback wires up in displayInit/uiInit. TODO Phase 0.
