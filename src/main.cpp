// Sonos Nest — standalone ESP32-S3 Sonos controller. Thin entry point: bring up the
// board, the unit's UI, and the shared core app, then hand off to the FreeRTOS tasks.
// Device-agnostic control lives in core/app.cpp; the UI in units/<unit>/; drivers in
// boards/<board>/. See plans/01-sonos-knob-controller-plan.md and plans/02-multi-unit-reorg.html.

#include <Arduino.h>

// The standalone bring-up modes (SD-as-USB, audio) replace the whole app and don't link the
// core/board/unit — so skip the app headers (and their LVGL/TJpg deps) in those builds.
#if !defined(SD_MSC_MODE) && !defined(AUDIO_BRINGUP)
#include "core/player_state.h"
#include "core/board.h"        // boardInit(), backlightSet()
#include "core/unit.h"         // uiInit()  (this build's unit)
#include "core/album_art.h"
#include "core/settings.h"
#include "core/net/ota.h"      // otaHandle()
#include "core/app.h"          // appBoot(), appStartTasks()
#endif

#ifdef PHASE0_BRINGUP
#include "boards/crowpanel_rotary/bringup.h"
#endif
#ifdef PHASE1_TEST
#include "boards/crowpanel_rotary/phase1_test.h"
#endif
#ifdef SD_MSC_MODE
#include "boards/es3c28p/sd_msc.h"
#endif
#ifdef AUDIO_BRINGUP
#include "boards/es3c28p/audio_test.h"
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

#if defined(SD_MSC_MODE)
  sdMscRun();    // does not return — expose the SD card to the host as a USB drive
#elif defined(AUDIO_BRINGUP)
  audioTestRun();  // does not return — play the ocean MP3 from SD through the ES8311 speaker
#else
  playerStateInit();
  settingsInit();       // NVS (persisted room, brightness, cached zones)

  if (!boardInit()) Serial.println("[boot] board init FAILED");  // display + touch + input
  backlightSet(settingsBrightness());   // restore saved brightness
  uiInit();             // build the unit's LVGL screens
  if (!albumArtInit()) Serial.println("[boot] album art buffer alloc failed (no PSRAM?)");

  appBoot();            // WiFi + time + OTA + Sonos discovery + zone selection
  appStartTasks();      // launch ui / net / art tasks
#endif  // bring-up modes
}

void loop() {
#if !defined(SD_MSC_MODE) && !defined(AUDIO_BRINGUP)
  // The loopTask hosts the OTA handler; everything else runs in dedicated tasks.
  otaHandle();
#endif
  vTaskDelay(pdMS_TO_TICKS(20));
}
