// PCF8574 I2C expander @ 0x21. Hosts LCD power/reset, touch reset/INT, and the KNOB
// PRESS (P5). The button must be debounced/edge-latched (or use the INT line) — polling
// at the UI tick can alias fast presses. See plan §3 (hard part #3).
#pragma once

#include <stdint.h>

void pcf8574Init();
uint8_t pcf8574Read();              // raw port byte
void    pcf8574Write(uint8_t mask); // drive output pins (LCD pwr/rst, touch rst)

bool knobPressed();   // debounced edge: true once per physical press
