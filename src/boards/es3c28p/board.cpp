// ES3C28P board — implements the board HAL (core/board.h) by delegating to this board's
// drivers. Like the nest's board.cpp, this file only provides boardInit() (the one-call
// bring-up) plus the no-op rotary/knob HAL — backlightSet lives in display.cpp.
//
// Status: display (ILI9341V over SPI + LVGL) is implemented. Touch (FT6336 @ 0x38), SD,
// mic, and RGB-LED are not yet wired.
#include "core/board.h"
#include "pins.h"
#include "display.h"
#include <Arduino.h>
#include <Wire.h>

bool boardInit() {
  bool ok = true;
  // The board owns the shared I2C bus (FT6336 touch + ES8311 audio codec sit on it). Begin
  // it now so the upcoming touch driver can use it; the display is SPI and doesn't need it.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  if (!displayInit()) { Serial.println("[board] es3c28p display init FAILED"); ok = false; }

  // TODO(next): touchInit() — register the FT6336 as an LVGL pointer indev (needs LVGL up).
  Serial.println("[board] es3c28p: display up; touch/SD/mic/LED not yet implemented");
  return ok;
}

// No rotary encoder / knob on this board — the touch-first UX drives everything.
int32_t   encoderDelta() { return 0; }
KnobEvent knobEvent()    { return KnobEvent::None; }
bool      knobPressed()  { return false; }
bool      knobDown()     { return false; }
