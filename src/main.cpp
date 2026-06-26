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
#include "sonos/soap_client.h"
#include "ui/screens.h"
#include "ui/album_art.h"

#ifdef PHASE0_BRINGUP
#include "hw/bringup.h"
#endif
#ifdef PHASE1_TEST
#include "net/phase1_test.h"
#endif

// Optional: pin a default room via include/secrets.h (#define SONOS_DEFAULT_ROOM "Name").
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// --- FreeRTOS tasks (mutex-guarded shared PlayerState) ---
static void uiTask(void *arg);     // LVGL render + input (encoder, button, touch)
static void netTask(void *arg);    // drain pending commands + poll transport/position/volume
static void artTask(void *arg);    // on track change: fetch art -> TJpg decode -> cache

// Volume + now-playing-room are the selected speaker; transport must hit its group
// COORDINATOR (plan §3), which differs when the speaker is grouped.
static String s_zoneName;
static String s_zoneIp;    // the selected speaker (volume target)
static String s_coordIp;   // its group coordinator (transport/queue/now-playing target)

// Pick the zone to control after discovery. Defaults to the first discovered zone; set
// SONOS_DEFAULT_ROOM in secrets.h to pin a specific room. (ROOMS picker is Phase 4.)
static void selectZone() {
  const std::vector<sonos::Zone> &zs = sonos::zones();
  if (zs.empty()) {
    Serial.println("[boot] no Sonos zones found — discovery will retry in netTask");
    return;
  }
  size_t idx = 0;
#ifdef SONOS_DEFAULT_ROOM
  for (size_t i = 0; i < zs.size(); ++i)
    if (zs[i].name == SONOS_DEFAULT_ROOM) { idx = i; break; }
#endif
  s_zoneName = zs[idx].name;
  s_zoneIp   = zs[idx].ip;
  s_coordIp  = sonos::coordinatorIpFor(s_zoneName);
  if (s_coordIp.length() == 0) s_coordIp = s_zoneIp;
  if (stateLock()) {
    g_player.zoneName        = zs[idx].name;
    g_player.coordinatorIp   = s_coordIp;
    g_player.coordinatorUuid = zs[idx].uuid;
    stateUnlock();
  }
  Serial.printf("[boot] zone %s @ %s, coordinator @ %s\n",
                s_zoneName.c_str(), s_zoneIp.c_str(), s_coordIp.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[sonos-nest] boot");

#ifdef PHASE0_BRINGUP
  bringupRun();  // does not return — serial subsystem self-test
#endif
#ifdef PHASE1_TEST
  phase1Run();   // does not return — WiFi + Sonos SOAP interactive test
#endif

  // Phase 0 — bring-up. Gate everything on flicker-free redraw while WiFi is active.
  playerStateInit();
  if (!pcf8574Init()) Serial.println("[boot] PCF8574 not responding — check I2C wiring");
  if (!displayInit()) Serial.println("[boot] display init FAILED");  // ST7701 RGB + LVGL
  touchInit();
  encoderInit();
  uiInit();             // build LVGL screens
  if (!albumArtInit()) Serial.println("[boot] album art buffer alloc failed (no PSRAM?)");

  // Phase 1 — network + Sonos discovery.
  wifiConnect();          // NVS creds -> connect; else captive portal (TODO)
  sonos::ssdpDiscover();  // SSDP once -> cache IPs/UUIDs in NVS; cache-first on boot (§3)
  selectZone();

  // Launch tasks. UI pinned to core 1; network work on core 0.
  xTaskCreatePinnedToCore(uiTask,  "ui",  8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(artTask, "art", 8192, nullptr, 1, nullptr, 0);
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

static void netTask(void *) {
  uint32_t lastPoll = 0;
  uint32_t lastVolCmd = 0;
  uint32_t lastCoordRefresh = 0;
  for (;;) {
    // If discovery failed at boot, keep retrying until we have a zone.
    if (s_zoneIp.length() == 0) {
      if (sonos::ssdpDiscover()) selectZone();
      if (s_zoneIp.length() == 0) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    }

    // Re-resolve the coordinator periodically (grouping changes at runtime).
    if (millis() - lastCoordRefresh > 5000) {
      lastCoordRefresh = millis();
      String c = sonos::coordinatorIpFor(s_zoneName);
      if (c.length()) {
        s_coordIp = c;
        if (stateLock()) { g_player.coordinatorIp = c; stateUnlock(); }
      }
    }

    // Drain pending commands (coalesced volume; latest wins).
    // Volume -> the speaker itself; transport -> the group coordinator.
    PendingCmds p;
    if (stateLock()) { p = g_pending; g_pending = PendingCmds(); stateUnlock(); }

    if (p.targetVolume >= 0) { sonos::setVolume(s_zoneIp, (uint8_t)p.targetVolume); lastVolCmd = millis(); }
    if (p.playPause) {
      TransportState st = TransportState::Unknown;
      if (sonos::getTransportInfo(s_coordIp, st))
        (st == TransportState::Playing) ? sonos::pause(s_coordIp) : sonos::play(s_coordIp);
    }
    if (p.prev) sonos::previous(s_coordIp);
    if (p.next) sonos::next(s_coordIp);

    // Poll ~1 Hz.
    if (millis() - lastPoll > 1000) {
      lastPoll = millis();
      TransportState st = TransportState::Unknown;
      PlayerState np;                       // scratch for position + now-playing metadata
      uint8_t vol = 0;
      bool gotVol = false;
      sonos::getTransportInfo(s_coordIp, st);   // transport state lives on the coordinator
      sonos::getPositionInfo(s_coordIp, np);
      // Volume is per-speaker; skip readback briefly after a local change (no UI snap-back).
      if (millis() - lastVolCmd > 1500) gotVol = sonos::getVolume(s_zoneIp, vol);

      if (stateLock()) {
        g_player.transport   = st;
        g_player.positionSec = np.positionSec;
        g_player.durationSec = np.durationSec;
        g_player.title       = np.title;
        g_player.artist      = np.artist;
        g_player.album       = np.album;
        g_player.artUri      = np.artUri;
        if (gotVol) g_player.volume = vol;
        g_player.dirty = true;
        stateUnlock();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

static void artTask(void *) {
  String last;
  for (;;) {
    String cur;
    if (stateLock()) { cur = g_player.artUri; stateUnlock(); }
    if (cur != last) {
      last = cur;
      if (cur.length()) albumArtFetch(cur);  // GET + TJpg decode + cache (never on UI task)
      else              albumArtClear();
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}
