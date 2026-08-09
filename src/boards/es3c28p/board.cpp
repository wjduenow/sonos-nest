// ES3C28P board — implements the board HAL (core/board.h) by delegating to this board's
// drivers. Like the nest's board.cpp, this file only provides boardInit() (the one-call
// bring-up) plus the no-op rotary/knob HAL — backlightSet lives in display.cpp.
//
// Status: display (ILI9341V over SPI + LVGL), touch (FT6336 @ 0x38), microSD, on-device audio
// (ES8311 codec) and the HTTP media/management server are implemented. The mic and the
// RGB-LED are not yet wired.
#include "core/board.h"
#include "pins.h"
#include "display.h"
#include "touch.h"
#include "local_stream.h"
#include <Arduino.h>
#include <Wire.h>

bool boardInit() {
  bool ok = true;
  // The board owns the shared I2C bus (FT6336 touch + ES8311 audio codec sit on it). Begin
  // it now so the upcoming touch driver can use it; the display is SPI and doesn't need it.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  // 100 kHz, not 400 kHz: the FT6336 on this board ACKs its address at 400 kHz but its
  // multi-byte register reads come back all-zeros — the bus routing/pull-ups can't sustain
  // fast-mode reads. 100 kHz is rock-solid for both the touch chip and the ES8311 codec.
  Wire.setClock(100000);

  if (!displayInit()) { Serial.println("[board] es3c28p display init FAILED"); ok = false; }
  touchInit();     // registers the FT6336 as an LVGL pointer indev (needs LVGL up)

  // HTTP server: Sonos media streaming + the remote SD-management UI. Its task waits for
  // WiFi on its own (appBoot() connects well after this), so starting it here is safe.
  mediaServerStart();

  Serial.println("[board] es3c28p: display + touch + httpd up; mic/LED not yet implemented");
  return ok;
}

// No rotary encoder / knob on this board — the touch-first UX drives everything.
int32_t   encoderDelta() { return 0; }
KnobEvent knobEvent()    { return KnobEvent::None; }
bool      knobPressed()  { return false; }
bool      knobDown()     { return false; }

// No speaker on this board (or none wired for UI feedback) — see core/board.h.
void uiSoundPlay(UiSound) {}

// has an SD card, but it is owned by local_tracks/local_stream as a MEDIA library;
// nothing has needed a firmware-writable data root here yet. Implement if that changes.
const char *localStorageRoot() { return nullptr; }

// On-die radio: a dead link is a Wi-Fi problem, not a transport one, so the normal reconnect
// path is the right and only recovery. See core/board.h.
bool netLinkRecover() { return false; }
