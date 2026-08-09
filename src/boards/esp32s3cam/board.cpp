// Board HAL for the nulllab/emakefun ESP32-S3-CAM — see core/board.h.
//
// A headless board: no display, no touch, no encoder, no SD, no audio, no mic. The whole
// external interface is four wires on header J4 (plans/04-sonos-button-plan.md §3):
//
//     J4.1  +5V   -> white  (ring +)
//     J4.2  GND   -> brown  (switch)
//     J4.6  IO47  -> black  (ring -, switched low-side by the pin itself — no MOSFET)
//     J4.7  IO14  -> brown  (switch)
//
// Most of core/board.h is stubbed here; that's the established pattern (crowpanel stubs audio
// and SD the same way), and it means webconfig/library/app need no special case for us.
#include "core/board.h"
#include "pins.h"
#include "button.h"
#include "config_server.h"
#include <Arduino.h>
#include <WiFi.h>          // boardConfigUrl() — WiFi.status()/localIP()

// --- Ring PWM ---------------------------------------------------------------------------
// LEDC channel 0. 5 kHz is well above flicker and far below anything the LED cares about.
static const int      RING_CH   = 0;
static const uint32_t RING_FREQ = 5000;
static const uint8_t  RING_RES  = 8;         // 8-bit: duty 0..255
static bool           s_pwmOn   = false;     // is LEDC currently attached to the pin?

bool boardInit() {
  buttonInit();

  // The ring is LOW-SIDE: the pin sinks the cathode, so LOW = lit. Drive the level BEFORE
  // enabling the output, and never leave this pin an input — floating, the node drifts toward
  // 5 V and only the ESD clamp stops it at ~4 V, over the 3.6 V abs-max. See pins.h.
  digitalWrite(PIN_RING_GATE, HIGH);
  pinMode(PIN_RING_GATE, OUTPUT);
  digitalWrite(PIN_RING_GATE, HIGH);         // start dark; the unit applies the saved level
  ledcSetup(RING_CH, RING_FREQ, RING_RES);

  // The onboard D5 (IO2 -> R6 1K -> D5, active-HIGH). Confirmed real from the schematic, but
  // NOT broken out to a header — only visible with the case open. Liveness tell, not product UI.
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);        // solid = powered

  configServerStart();                       // its task waits for WiFi itself

  return true;                               // no display to fail
}

// The ring, driven through the backlight HAL. On a screenless box "the backlight" is the only
// light there is, so this reuses the existing call rather than adding one every other board
// would have to stub — the same instinct as mapping the button onto knobEvent() below.
//
// INVERTED, because low-side: pct 100 -> duty 0 -> pin held LOW -> fully lit.
void backlightSet(uint8_t pct) {
  if (pct > 100) pct = 100;

  if (pct == 0) {
    // Not just duty=255: that still leaves a ~0.4% on-pulse, and a white LED at ~60 uA average
    // is a visible glow in a dark bedroom — exactly what "off" is supposed to fix. Detach and
    // hold the pin high so off is genuinely off.
    if (s_pwmOn) { ledcDetachPin(PIN_RING_GATE); s_pwmOn = false; }
    pinMode(PIN_RING_GATE, OUTPUT);
    digitalWrite(PIN_RING_GATE, HIGH);
    return;
  }

  if (!s_pwmOn) { ledcAttachPin(PIN_RING_GATE, RING_CH); s_pwmOn = true; }
  ledcWrite(RING_CH, 255 - ((uint32_t)pct * 255 / 100));
}

// --- Rotary input: there is no encoder, but the button IS a press-classified momentary, which
// is exactly what the knob HAL describes. Mapping onto it costs no core change (§5).
int32_t   encoderDelta() { return 0; }
KnobEvent knobEvent()    { return buttonEvent(); }
bool      knobPressed()  { return buttonEvent() == KnobEvent::Short; }
bool      knobDown()     { return buttonDown(); }

// --- Everything this board doesn't have -------------------------------------------------
bool localAudioPlay(const char *)     { return false; }
void localAudioStop()                 {}
bool localAudioActive()               { return false; }
void localAudioSetVolume(uint8_t)     {}

bool        wakeWordInit()            { return false; }
int         wakeWordPoll()            { return -1; }
const char *wakeWordPhrase(int)       { return nullptr; }
int         wakeWordCount()           { return 0; }

const char *localFileUrl(const char *) { return nullptr; }   // no local storage to serve

// The config page is this board's whole UI, but localManagerUrl() means "a file manager", which
// we don't have. Reporting nullptr keeps that honest; the URL is printed to serial at boot.
const char *localManagerUrl()          { return nullptr; }

// ...but the button DOES serve a web config page (config_server.cpp, port 8080) — that's what the
// portal's "Open config" should point at. Valid whenever WiFi is up. Port mirrors CONFIG_PORT.
const char *boardConfigUrl() {
  if (WiFi.status() != WL_CONNECTED) return nullptr;
  static String url;
  url = "http://" + WiFi.localIP().toString() + ":8080";
  return url.c_str();
}

void        localTracksRefresh()       {}
int         localTrackCount()          { return 0; }
const char *localTrackName(int)        { return nullptr; }
const char *localTrackPath(int)        { return nullptr; }

// No speaker on this board (or none wired for UI feedback) — see core/board.h.
void uiSoundPlay(UiSound) {}

// no storage wired up on this board.
const char *localStorageRoot() { return nullptr; }

// On-die radio: a dead link is a Wi-Fi problem, not a transport one, so the normal reconnect
// path is the right and only recovery. See core/board.h.
bool netLinkRecover() { return false; }
