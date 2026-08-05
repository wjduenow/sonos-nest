// Board HAL — ELECROW CrowPanel Advance 7" ESP32-P4 (DHE04107D). Implements core/board.h.
//
// What this board HAS: 1024x600 MIPI-DSI panel (EK79007), GT911 touch, two speakers on an NS4168
// amp, a PDM mic, a microSD slot, and Wi-Fi via an ESP32-C6 over SDIO.
//
// What it does NOT have on-board: any rotary encoder or push buttons. The design system's Ø36 dial
// and 4× Ø13 transport caps are external hardware. They do NOT hang off the 11-pin GPIO header as
// once planned — both go on the **shared I2C bus** via J13 (Crowtail), which is why there is no
// PCNT/encoder pin here: the dial is an Arduino Modulino Knob at 0x76 (see knob.cpp) and the
// buttons will be a PCF8574 at 0x20. See plans/07-sonos-jukebox.md for the buttonCount/buttonPoll/
// buttonName HAL sketch, still to be written.
//
// Wi-Fi note: nothing here configures ESP-Hosted. The C6's SDIO pins — including the reset line
// that took a while to find — come from variants/crowpanel_p4_7in/pins_arduino.h. Do not add a
// WiFi.setPins() call here; the variant is the single source of truth.
#include <Arduino.h>
#include <Wire.h>

#include "core/board.h"
#include "display.h"
#include "i2c_bus.h"
#include "knob.h"
#include "pins.h"
#include "sd_card.h"
#include "touch.h"
#include "web_config.h"
#include "ui_sound.h"

bool boardInit() {
  Serial.printf("[board ] CrowPanel Advance 7\" ESP32-P4 — %s, %lu KB internal heap free\n",
                ESP.getChipModel(), (unsigned long)(ESP.getFreeHeap() / 1024));

  // Shared I2C bus: GT911 touch, and whatever else is on the Crowtail connectors. (An
  // unidentified device also answers at 0x2F — not in Elecrow's documentation, harmless so far.)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  i2cBusInit();   // before ANY driver touches the bus — see i2c_bus.h

  if (!displayInit()) {
    Serial.println("[board ] display init FAILED — see the [display] line above");
    return false;
  }
  // Touch failing is not fatal: a jukebox that renders but cannot be touched is still worth
  // booting (it shows what is playing, and the physical controls are the eventual primary input).
  if (!touchInit()) Serial.println("[board ] continuing without touch");

  // Nor is audio: silent feedback is a downgrade, not a failure.
  if (!uiSoundInit()) Serial.println("[board ] continuing without UI tones");

  // Nor is the dial. knobInit() returning false only means it is not plugged in *yet* — its task
  // keeps rescanning the bus, so the dial can be connected later without a reflash.
  knobInit();

  // Nor is storage: without a card the Radio page has no cache and says so, but everything that
  // talks to Sonos directly is unaffected. localStorageRoot() returns nullptr and callers skip.
  if (!sdCardInit()) Serial.println("[board ] continuing without SD storage");

  // Config page. Its task waits for WiFi itself, since appBoot() connects after this returns.
  if (!webConfigServerInit()) Serial.println("[board ] continuing without the config server");

  return true;
}

// backlightSet() lives in display.cpp, next to the LEDC setup it depends on.

// --- Rotary input lives in knob.cpp (I2C, not GPIO) ---------------------------

// --- Local audio: no SD-backed media playback on this unit --------------------
// The board DOES have speakers, but this contract means "play a file off local storage", which
// this unit has no concept of. UI feedback tones are a separate, smaller thing — see the
// UI-sound-feedback section of plans/07-sonos-jukebox.md — and live in ui_sound.cpp instead.
bool localAudioPlay(const char *)   { return false; }
void localAudioStop()               {}
bool localAudioActive()             { return false; }
void localAudioSetVolume(uint8_t)   {}

// --- Wake word: the board has a PDM mic, but no engine is wired up ------------
bool        wakeWordInit()      { return false; }
int         wakeWordPoll()      { return -1; }
const char *wakeWordPhrase(int) { return nullptr; }
int         wakeWordCount()     { return 0; }

// --- Local files / web UI: none ------------------------------------------------
const char *localFileUrl(const char *) { return nullptr; }
const char *localManagerUrl()          { return nullptr; }
void        localTracksRefresh()       {}
int         localTrackCount()          { return 0; }
const char *localTrackName(int)        { return nullptr; }
const char *localTrackPath(int)        { return nullptr; }
