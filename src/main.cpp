// Sonos Nest — standalone ESP32-S3 Sonos controller. Thin entry point: bring up the
// board, the unit's UI, and the shared core app, then hand off to the FreeRTOS tasks.
// Device-agnostic control lives in core/app.cpp; the UI in units/<unit>/; drivers in
// boards/<board>/. See plans/01-sonos-knob-controller-plan.md and plans/02-multi-unit-reorg.html.

#include <Arduino.h>

#include "core/player_state.h"
#include "core/board.h"        // boardInit(), backlightSet()
#include "core/unit.h"         // uiInit()  (this build's unit)
#include "core/album_art.h"
#include "core/settings.h"
#include "core/net/ota.h"      // otaHandle()
#include "core/app.h"          // appBoot(), appStartTasks()

#ifdef PHASE0_BRINGUP
#include "boards/crowpanel_rotary/bringup.h"
#endif
#ifdef PHASE1_TEST
#include "boards/crowpanel_rotary/phase1_test.h"
#endif

// Per-unit mDNS/OTA name; set by the build env (-DDEVICE_HOSTNAME). Default keeps
// non-env builds working.
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "sonos-nest"
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[" DEVICE_HOSTNAME "] boot");

#ifdef PHASE0_BRINGUP
  bringupRun();  // does not return — serial subsystem self-test
#endif
#ifdef PHASE1_TEST
  phase1Run();   // does not return — WiFi + Sonos SOAP interactive test
#endif

  playerStateInit();
  settingsInit();       // NVS (persisted room, brightness, cached zones)

  if (!boardInit()) Serial.println("[boot] board init FAILED");  // display + touch + input
  backlightSet(settingsBrightness());   // restore saved brightness
  uiInit();             // build the unit's LVGL screens
  if (!albumArtInit()) Serial.println("[boot] album art buffer alloc failed (no PSRAM?)");

  appBoot();            // WiFi + time + OTA + Sonos discovery + zone selection
  appStartTasks();      // launch ui / net / art tasks
}

void loop() {
  // The loopTask hosts the OTA handler; everything else runs in dedicated tasks.
  otaHandle();
  vTaskDelay(pdMS_TO_TICKS(20));
}
