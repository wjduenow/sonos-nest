// CrowPanel 2.1" rotary board — implements the board HAL (core/board.h) by delegating to
// this board's drivers (display, touch, encoder, PCF8574 expander). backlightSet /
// encoderDelta / knobEvent / knobPressed / knobDown are defined in those drivers; this
// file only provides boardInit(), the one-call bring-up.
#include "core/board.h"
#include "pins.h"
#include "display.h"
#include "touch.h"
#include "encoder.h"
#include "pcf8574.h"
#include <Arduino.h>
#include <Wire.h>

bool boardInit() {
  bool ok = true;
  // The board owns the shared I2C bus (the PCF8574 + touch controller sit on it).
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  // The PCF8574 hosts LCD/touch power+reset and the knob button, so it must init before
  // the display/touch that depend on it.
  if (!pcf8574Init()) { Serial.println("[board] PCF8574 not responding — check I2C wiring"); ok = false; }
  if (!displayInit()) { Serial.println("[board] display init FAILED"); ok = false; }  // ST7701 RGB + LVGL
  touchInit();     // registers the CST816 as an LVGL pointer indev (needs LVGL up)
  encoderInit();   // EC11 quadrature via PCNT
  return ok;
}

// No onboard audio codec/speaker on this board — local playback is a no-op here.
bool localAudioPlay(const char * /*path*/) { return false; }
void localAudioStop() {}
bool localAudioActive() { return false; }
void localAudioSetVolume(uint8_t /*pct*/) {}
const char *localFileUrl(const char * /*path*/) { return nullptr; }
