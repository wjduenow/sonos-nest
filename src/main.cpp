// Sonos Nest — standalone ESP32-S3 Sonos controller. Thin entry point: bring up the
// board, the unit's UI, and the shared core app, then hand off to the FreeRTOS tasks.
// Device-agnostic control lives in core/app.cpp; the UI in units/<unit>/; drivers in
// boards/<board>/. See plans/01-sonos-knob-controller-plan.md and plans/02-multi-unit-reorg.html.

#include <Arduino.h>

// The standalone bring-up modes (SD-as-USB, audio, mic, wake, button) replace the whole app and
// don't link the core/board/unit — so skip the app headers (and their LVGL/TJpg deps) in those
// builds.
#if !defined(SD_MSC_MODE) && !defined(AUDIO_BRINGUP) && !defined(MIC_BRINGUP) && \
    !defined(WAKE_BRINGUP) && !defined(BUTTON_BRINGUP) && !defined(BUTTON_V2_BRINGUP)
#include "core/player_state.h"
#include "core/board.h"        // boardInit(), backlightSet()
#include "core/unit.h"         // uiInit()  (this build's unit)
#ifndef HEADLESS
#include "core/ui/album_art.h"
#endif
#include "core/settings.h"
#include "core/net/ota.h"      // otaHandle()
#include "core/net/logmirror.h"  // LOG — must be included OUTSIDE any __has_include("secrets.h")
                                 // guard, or LOG only exists on machines that happen to have the
                                 // gitignored header (CLAUDE.md; this has cost time before)
#include "core/crashlog.h"     // read back the last panic's core dump
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
#ifdef MIC_BRINGUP
#include "boards/es3c28p/mic_test.h"
#endif
#ifdef WAKE_BRINGUP
#include "boards/es3c28p/wake_test.h"
#endif
#ifdef BUTTON_BRINGUP
#include "boards/esp32s3cam/bringup.h"
#endif
#ifdef BUTTON_V2_BRINGUP
#include "boards/xiao_esp32s3/bringup.h"
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
#elif defined(MIC_BRINGUP)
  micTestRun();    // does not return — capture the ES8311 mic and print a live level meter
#elif defined(WAKE_BRINGUP)
  wakeTestRun();   // does not return — TFLM + microWakeWord bring-up
#elif defined(BUTTON_BRINGUP)
  camBringupRun(); // does not return — ESP32-S3-CAM button + LED + memory self-test
#elif defined(BUTTON_V2_BRINGUP)
  xiaoBringupRun(); // does not return — XIAO ESP32S3 LED polarity + button + ring self-test
#else
  playerStateInit();
  settingsInit();       // NVS (persisted room, brightness, cached zones)
  crashlog::begin();    // read any stored core dump now; only touches flash, so it is safe this
                        // early. Printed after appBoot() below, once there is a link for the log
                        // mirror to carry it out on.

  if (!boardInit()) Serial.println("[boot] board init FAILED");  // display + touch + input
  backlightSet(settingsBrightness());   // restore saved brightness
  uiInit();             // build the unit's LVGL screens
#ifndef HEADLESS
  if (!albumArtInit()) Serial.println("[boot] album art buffer alloc failed (no PSRAM?)");
#endif

  appBoot();            // WiFi + time + OTA + Sonos discovery + zone selection

  // Printed HERE, not at the top of setup(): the log mirror only delivers to clients connected at
  // the time, and at the top of setup() there is no link and therefore no client — the report
  // would go into the ring buffer and be dropped. After appBoot() a watcher that reconnects on
  // the device coming back has had several seconds to attach. It is still best-effort, which is
  // why the same data is in /api/config → health.crash, where it can be pulled at any time.
  crashlog::report(LOG);
  wakeWordInit();       // mic -> wake-word engine (no-op / false on boards without a mic).
                        // After appBoot so it doesn't compete with WiFi/discovery for CPU at boot.
  appStartTasks();      // launch ui / net / art tasks
#endif  // bring-up modes
}

void loop() {
#if !defined(SD_MSC_MODE) && !defined(AUDIO_BRINGUP) && !defined(MIC_BRINGUP) && \
    !defined(WAKE_BRINGUP) && !defined(BUTTON_BRINGUP) && !defined(BUTTON_V2_BRINGUP)
  // The loopTask hosts the OTA handler; everything else runs in dedicated tasks.
  otaHandle();
  // ...and watches netTask, which has been observed stopping while every other task keeps running
  // (see app.h). loopTask is the right host precisely because it is independent of netTask and
  // already runs here — no extra task, no extra stack, no per-unit change.
  appSupervisorTick();
#endif
  vTaskDelay(pdMS_TO_TICKS(20));
}
