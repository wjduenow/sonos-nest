#include "pcf8574.h"
#include "../board_pins.h"
#include <Wire.h>

// TODO Phase 0:
//  - Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); init expander at PCF8574_ADDR.
//  - Drive LCD power (P3) / LCD reset (P4) / touch reset (P0) for display bring-up.
//  - Read PCF_PIN_KNOB_BTN (P5) with debounce/edge-latch for play/pause.

void pcf8574Init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  // TODO
}

uint8_t pcf8574Read()            { return 0xFF; /* TODO */ }
void    pcf8574Write(uint8_t)    { /* TODO */ }
bool    knobPressed()            { return false; /* TODO: debounced edge */ }
