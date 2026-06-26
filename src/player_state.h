// Shared, mutex-guarded player state. Written by poll_task / art_task, read by ui_task.
// See plans/01-sonos-knob-controller-plan.md §6.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class TransportState { Stopped, Playing, Paused, Transitioning, Unknown };

struct PlayerState {
  // Now playing
  String        title;
  String        artist;
  String        album;
  String        artUri;          // http://<ip>:1400/getaa?...  (plain HTTP, no TLS)
  uint32_t      positionSec = 0;
  uint32_t      durationSec = 0;
  TransportState transport  = TransportState::Unknown;
  uint8_t       volume      = 0; // 0-100
  bool          muted       = false;

  // Target zone (resolves to the group COORDINATOR for transport calls — see §3)
  String        zoneName;
  String        coordinatorIp;
  String        coordinatorUuid;

  bool          dirty = false;   // set by writers, cleared by ui_task after redraw
};

// Commands posted by ui_task (input) and drained by net_task (SOAP). Volume coalesces:
// only the latest target is sent. Guarded by g_stateMutex like PlayerState.
struct PendingCmds {
  int    targetVolume = -1;   // -1 = none; else 0..100 to apply
  int    setPlay      = -1;   // -1 = none; 0 = pause; 1 = play (explicit, decided by the UI)
  bool   next         = false;
  bool   prev         = false;
  String requestZoneIp;       // non-empty: switch the controlled zone to this speaker IP
  String groupJoinIp;         // join this speaker to the active group
  String groupLeaveIp;        // remove this speaker from its group (become standalone)
};

// Bumped whenever the discovered zone list changes (after grouping ops / re-discovery) so
// the UI can re-render the room/group lists.
extern volatile uint32_t g_zonesGen;

// Guard every read/write of the global PlayerState / PendingCmds with this mutex.
extern SemaphoreHandle_t g_stateMutex;
extern PlayerState       g_player;
extern PendingCmds       g_pending;

void playerStateInit();

// RAII-ish helpers for the common short critical sections.
inline bool stateLock()   { return xSemaphoreTake(g_stateMutex, portMAX_DELAY) == pdTRUE; }
inline void stateUnlock() { xSemaphoreGive(g_stateMutex); }
