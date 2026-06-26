// Sonos Nest — standalone ESP32-S3 rotary-knob Sonos controller.
// Boot sequence + FreeRTOS task launch. See plans/01-sonos-knob-controller-plan.md §6.

#include <Arduino.h>

#include "board_pins.h"
#include "player_state.h"
#include "hw/display.h"
#include "hw/touch.h"
#include "hw/encoder.h"
#include "hw/pcf8574.h"
#include "net/wifi.h"
#include "sonos/ssdp.h"
#include "ui/screens.h"

#ifdef PHASE0_BRINGUP
#include "hw/bringup.h"
#endif

// --- FreeRTOS tasks (mutex-guarded shared PlayerState) ---
static void uiTask(void *arg);     // LVGL render + input (encoder, button, touch)
static void pollTask(void *arg);   // every 1-2s: GetTransportInfo/PositionInfo/Volume
static void artTask(void *arg);    // on track change: fetch art -> TJpg decode -> cache

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[sonos-nest] boot");

#ifdef PHASE0_BRINGUP
  bringupRun();  // does not return — serial subsystem self-test
#endif

  // Phase 0 — bring-up. Gate everything on flicker-free redraw while WiFi is active.
  playerStateInit();
  if (!pcf8574Init()) Serial.println("[boot] PCF8574 not responding — check I2C wiring");
  if (!displayInit()) Serial.println("[boot] display init FAILED");  // ST7701 RGB + LVGL
  touchInit();
  encoderInit();
  uiInit();             // build LVGL screens

  // Phase 1 — network + Sonos discovery.
  wifiConnect();          // NVS creds -> connect; else captive portal (TODO)
  sonos::ssdpDiscover();  // SSDP once -> cache IPs/UUIDs in NVS; cache-first on boot (§3)

  // Launch tasks. UI pinned to core 1; network work on core 0.
  xTaskCreatePinnedToCore(uiTask,   "ui",   8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(pollTask, "poll", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(artTask,  "art",  8192, nullptr, 1, nullptr, 0);
}

void loop() {
  // All work happens in tasks. Nothing here.
  vTaskDelay(portMAX_DELAY);
}

static void uiTask(void *) {
  for (;;) {
    uiTick();                 // lv_timer_handler() + input handling
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void pollTask(void *) {
  for (;;) {
    // TODO Phase 2: GetTransportInfo + GetPositionInfo + GetVolume on the coordinator,
    //               write into g_player under g_stateMutex.
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}

static void artTask(void *) {
  for (;;) {
    // TODO Phase 2: on artUri change, fetch (plain HTTP), TJpg decode + downscale,
    //               cache as an LVGL image. Never decode per poll.
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
