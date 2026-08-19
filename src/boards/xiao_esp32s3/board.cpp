// Board HAL for the Seeed XIAO ESP32S3 — see core/board.h.
//
// The `button-v2` unit: the same product as `sonos-button`, on a board a quarter the size. A
// headless board — no display, no touch, no encoder, no SD, no audio, no mic. The whole external
// interface is four wires soldered to the right-hand pad rail (pins.h):
//
//     5V   -> white  (ring +)
//     GND  -> brown  (switch)
//     D10  -> black  (ring -, switched low-side by the pin itself — no MOSFET)
//     D9   -> brown  (switch)
//
// Deliberately a near-copy of boards/esp32s3cam/board.cpp: same silicon, same switch, same ring,
// same stubs. What the two boards actually share — the press classifier and the :8080 config
// page — lives in boards/button_common/ rather than being duplicated here. See its README.
#include "core/board.h"
#include "pins.h"
#include "boards/button_common/button.h"
#include "boards/button_common/config_server.h"
#include <Arduino.h>
#include <WiFi.h>          // boardConfigUrl() — WiFi.status()/localIP()

// --- Ring PWM ---------------------------------------------------------------------------
// LEDC channel 0. 5 kHz is well above flicker and far below anything the LED cares about.
// Arduino 2.0.17 API (ledcSetup/ledcAttachPin), matching the pinned platform — see platformio.ini.
static const int      RING_CH   = 0;
static const uint32_t RING_FREQ = 5000;
static const uint8_t  RING_RES  = 8;         // 8-bit: duty 0..255
static bool           s_pwmOn   = false;     // is LEDC currently attached to the pin?

// The onboard user LED, hidden inside the case. Polarity is a board property and is still
// unverified on this one — see pins.h. Routed through these two so a bring-up correction is a
// one-line change in pins.h rather than a hunt through call sites.
static inline void statusLed(bool on) {
#if PIN_STATUS_LED_ACTIVE_LOW
  digitalWrite(PIN_STATUS_LED, on ? LOW : HIGH);
#else
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
#endif
}

bool boardInit() {
  buttonInit(PIN_BUTTON);

  // The ring is LOW-SIDE: the pin sinks the cathode, so LOW = lit. Drive the level BEFORE
  // enabling the output, and never leave this pin an input — floating, the node drifts toward
  // 5 V and only the ESD clamp stops it at ~4 V, over the 3.6 V abs-max. See pins.h.
  digitalWrite(PIN_RING_GATE, HIGH);
  pinMode(PIN_RING_GATE, OUTPUT);
  digitalWrite(PIN_RING_GATE, HIGH);         // start dark; the unit applies the saved level
  ledcSetup(RING_CH, RING_FREQ, RING_RES);

  pinMode(PIN_STATUS_LED, OUTPUT);
  statusLed(true);                           // solid = powered

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
// is exactly what the knob HAL describes. Mapping onto it costs no core change (plans/04 §5).
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

// ...but the button DOES serve a web config page (button_common/config_server.cpp, port 8080) —
// that's what the portal's "Open config" should point at. Valid whenever WiFi is up. Port
// mirrors CONFIG_PORT.
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

// No speaker on this board — see core/board.h.
void uiSoundPlay(UiSound) {}

// no storage wired up on this board.
const char *localStorageRoot() { return nullptr; }

// On-die radio: a dead link is a Wi-Fi problem, not a transport one, so the normal reconnect
// path is the right and only recovery. See core/board.h.
bool netLinkRecover() { return false; }
