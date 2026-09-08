// Board HAL for the Waveshare ESP32-S3-LCD-1.47B — see core/board.h.
//
// The `button-v3` unit: the same product as sonos-button and button-v2 — press to start the
// configured playlist, press again to stop — plus a 1.47" ST7789 that is DARK by default and wakes
// on a tap or a press to show a QR code. That code is the answer to the two things a screenless
// button cannot tell you: which Wi-Fi setup AP to join before it is provisioned, and what its own
// address is afterwards.
//
// Still -DHEADLESS. That macro means "no rich Now Playing" — no album art, a 3 s Sonos poll, no
// core/ui/ — and all of that is still true here. Having a signpost panel does not make this a
// screen unit; see lib/qrcodegen/VENDORING.md for why it does not link LVGL either.
//
// Shares boards/button_common/ (press classifier + the :8080 config page) with both other buttons
// and runs units/sleep_button/ unchanged — one UX, three boards.
#include "core/board.h"
#include "core/settings.h"     // settingsBrightness() for the panel — see core/board.h's infoScreen*
#include "pins.h"
#include "display.h"
#include "imu.h"
#include "boards/button_common/button.h"
#include "boards/button_common/config_server.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>              // boardConfigUrl() — WiFi.status()/localIP()
#include "core/net/logmirror.h"

// --- Ring PWM ---------------------------------------------------------------------------
// LEDC channel 0, exactly as on the other two buttons. The LCD BACKLIGHT is channel 1
// (display.cpp) — the one thing on this board that the other two did not have to coordinate.
static const int      RING_CH   = 0;
static const uint32_t RING_FREQ = 5000;
static const uint8_t  RING_RES  = 8;
static bool           s_pwmOn   = false;

bool boardInit() {
  buttonInit(PIN_BUTTON);

  // The ring is LOW-SIDE: the pin sinks the cathode, so LOW = lit. Drive the level BEFORE enabling
  // the output, and never leave this pin an input — floating, the node drifts toward 5 V and only
  // the ESD clamp stops it at ~4 V, over the 3.6 V abs-max. See pins.h.
  digitalWrite(PIN_RING_GATE, HIGH);
  pinMode(PIN_RING_GATE, OUTPUT);
  digitalWrite(PIN_RING_GATE, HIGH);         // start dark; the unit applies the saved level
  ledcSetup(RING_CH, RING_FREQ, RING_RES);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  imuInit();                                 // false is survivable — press still wakes the screen

  const bool disp = displayInit();           // comes up blank with the backlight off
  if (!disp) LOG.println("[board  ] ST7789 init FAILED — the button still works, blind");

  configServerStart();                       // its task waits for WiFi itself

  // Deliberately NOT `return disp`. On a screen unit a dead panel means a dead product, but this
  // one is a BUTTON: with no display it degrades to exactly a sonos-button, which is a working
  // device. Reporting failure here would only make main.cpp print "board init FAILED" about a
  // unit that is about to work fine.
  return true;
}

// --- Light -------------------------------------------------------------------------------
// On this board these are genuinely two different lamps, which is why core/board.h had to split
// them: backlightSet() is the LCD, ringSet() is the illuminated button.
void backlightSet(uint8_t pct) { displayBacklight(pct); }

// INVERTED, because low-side: pct 100 -> duty 0 -> pin held LOW -> fully lit.
void ringSet(uint8_t pct) {
  if (pct > 100) pct = 100;

  if (pct == 0) {
    // Not just duty=255: that still leaves a ~0.4% on-pulse, and a white LED at ~60 uA average is
    // a visible glow in a dark bedroom — exactly what "off" is supposed to fix.
    if (s_pwmOn) { ledcDetachPin(PIN_RING_GATE); s_pwmOn = false; }
    pinMode(PIN_RING_GATE, OUTPUT);
    digitalWrite(PIN_RING_GATE, HIGH);
    return;
  }

  if (!s_pwmOn) { ledcAttachPin(PIN_RING_GATE, RING_CH); s_pwmOn = true; }
  ledcWrite(RING_CH, 255 - ((uint32_t)pct * 255 / 100));
}

// --- Info screen -------------------------------------------------------------------------
bool infoScreenPresent() { return true; }

void infoScreenShow(const char *qrText, const char *caption,
                    const char *const *lines, uint8_t nLines) {
  displayQrPage(qrText, caption, lines, nLines);
  // Paint first, THEN light it. The other order shows the previous page — or a black flash — for
  // the ~40 ms the repaint takes, right in front of someone who just tapped the box.
  displayBacklight(settingsBrightness());
}

void infoScreenOff() { displayBlank(); }

// --- Rotary input: there is no encoder, but the button IS a press-classified momentary, which is
// exactly what the knob HAL describes. Mapping onto it costs no core change (plans/04 §5).
int32_t   encoderDelta() { return 0; }
KnobEvent knobEvent()    { return buttonEvent(); }
bool      knobPressed()  { return buttonEvent() == KnobEvent::Short; }
bool      knobDown()     { return buttonDown(); }
bool      tapDetected()  { return imuTapDetected(); }

// --- Everything this board doesn't have (or doesn't use) --------------------------------
bool localAudioPlay(const char *)     { return false; }
void localAudioStop()                 {}
bool localAudioActive()               { return false; }
void localAudioSetVolume(uint8_t)     {}

bool        wakeWordInit()            { return false; }
int         wakeWordPoll()            { return -1; }
const char *wakeWordPhrase(int)       { return nullptr; }
int         wakeWordCount()           { return 0; }

// The board HAS a microSD slot (pins.h records the pins), but this unit stores nothing, so it is
// never mounted. nullptr is the honest answer and callers are required to treat it as "feature
// unavailable" rather than an error.
const char *localFileUrl(const char *) { return nullptr; }
const char *localStorageRoot()         { return nullptr; }
void        localTracksRefresh()       {}
int         localTrackCount()          { return 0; }
const char *localTrackName(int)        { return nullptr; }
const char *localTrackPath(int)        { return nullptr; }

// localManagerUrl() means specifically a FILE manager, which we have no files for.
const char *localManagerUrl()          { return nullptr; }

// ...but the button DOES serve a web config page (button_common/config_server.cpp, port 8080).
// That is what the portal's "Open config" points at, and it is also what the on-screen QR encodes.
const char *boardConfigUrl() {
  if (WiFi.status() != WL_CONNECTED) return nullptr;
  static String url;
  url = "http://" + WiFi.localIP().toString() + ":8080";
  return url.c_str();
}

// No speaker on this board — see core/board.h.
void uiSoundPlay(UiSound) {}

// On-die radio: a dead link is a Wi-Fi problem, not a transport one, so the normal reconnect path
// is the right and only recovery. See core/board.h.
bool netLinkRecover(const char *) { return false; }
