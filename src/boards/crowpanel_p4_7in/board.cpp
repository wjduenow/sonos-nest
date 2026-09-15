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
#include "core/net/logmirror.h"
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
#ifdef HOSTED_DIAG
#include "esp_log.h"
#endif

#ifdef HOSTED_DIAG
// Diagnostic build for the ESP-Hosted link death (#26, plans/13). The env raises
// CONFIG_LOG_MAXIMUM_LEVEL to INFO and enables CONFIG_ESP_HOSTED_PKT_STATS, while the default
// level stays ERROR so the rest of IDF keeps quiet. Only these tags are raised, and this must run
// before appBoot() starts ESP-Hosted so the stats timer logs from its first tick.
//   stats       — s2h/h2s packet counters + flow control + internal heap, every 2 s (the point)
//   H_SDIO_DRV  — RX/TX mempool OOM, "RX buffer alloc failed", dropped packets, queue-full
// RPC and transport stay at WARN: rpc_core logs EVERY request at INFO, and the RSSI read alone
// makes one per network pass — it would bury the stats and slow the 115200 console.
static void hostedDiagLogLevels() {
  esp_log_level_set("stats", ESP_LOG_INFO);
  esp_log_level_set("H_SDIO_DRV", ESP_LOG_INFO);
  for (const char *tag : {"sdio_wrapper", "transport", "H_API", "rpc_core", "rpc_rsp", "rpc_req",
                          "rpc_utils", "RPC_WRAP"})
    esp_log_level_set(tag, ESP_LOG_WARN);
  LOG.println("[board ] HOSTED_DIAG: esp_hosted stats + H_SDIO_DRV at INFO, rpc/transport at WARN");
}
#endif

bool boardInit() {
#ifdef HOSTED_DIAG
  hostedDiagLogLevels();
#endif
  LOG.printf("[board ] CrowPanel Advance 7\" ESP32-P4 — %s, %lu KB internal heap free\n",
                ESP.getChipModel(), (unsigned long)(ESP.getFreeHeap() / 1024));

  // Shared I2C bus: GT911 touch, and whatever else is on the Crowtail connectors. (An
  // unidentified device also answers at 0x2F — not in Elecrow's documentation, harmless so far.)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  i2cBusInit();   // before ANY driver touches the bus — see i2c_bus.h

  if (!displayInit()) {
    LOG.println("[board ] display init FAILED — see the [display] line above");
    return false;
  }
  // Touch failing is not fatal: a jukebox that renders but cannot be touched is still worth
  // booting (it shows what is playing, and the physical controls are the eventual primary input).
  if (!touchInit()) LOG.println("[board ] continuing without touch");

  // Nor is audio: silent feedback is a downgrade, not a failure.
  if (!uiSoundInit()) LOG.println("[board ] continuing without UI tones");

  // Nor is the dial. knobInit() returning false only means it is not plugged in *yet* — its task
  // keeps rescanning the bus, so the dial can be connected later without a reflash.
  knobInit();

  // Nor is storage: without a card the Radio page has no cache and says so, but everything that
  // talks to Sonos directly is unaffected. localStorageRoot() returns nullptr and callers skip.
  if (!sdCardInit()) LOG.println("[board ] continuing without SD storage");

  // Config page. Its task waits for WiFi itself, since appBoot() connects after this returns.
  if (!webConfigServerInit()) LOG.println("[board ] continuing without the config server");

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
