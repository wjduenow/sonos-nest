// PCF8574 I2C expander @ 0x21. Hosts LCD power/reset, touch reset/INT, and the KNOB
// PRESS (P5). The button must be debounced/edge-latched (or use the INT line) — polling
// at the UI tick can alias fast presses. See plan §3 (hard part #3).
//
// PCF8574 is quasi-bidirectional: a pin used as an INPUT must have a 1 written to it
// (weak pull-up); reading then reflects the external level. We keep a shadow byte with
// the input pins (button P5, touch INT P2) held high and only drive the output pins.
#pragma once

#include <stdint.h>

bool    pcf8574Init();              // returns false if the expander doesn't ACK on I2C
uint8_t pcf8574Read();              // raw port byte (input pins reflect external level)
void    pcf8574Write(uint8_t mask); // write the full port (clobbers shadow)

void    pcfSetPin(uint8_t pin, bool high);  // set one output pin, preserve the rest
bool    pcfGetPin(uint8_t pin);             // read one pin level

// Display bring-up sequencing (driven from displayInit / touchInit).
void    pcfLcdPower(bool on);
void    pcfLcdReset();              // pulse LCD reset low->high
void    pcfTouchReset();           // pulse touch reset low->high

bool    knobPressed();             // debounced edge: true once per physical press
bool    knobDown();                // current debounced level (true while held)
