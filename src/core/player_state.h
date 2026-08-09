// Shared, mutex-guarded player state. Written by poll_task / art_task, read by ui_task.
// See plans/01-sonos-knob-controller-plan.md §6.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

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
  String        currentUri;      // AVTransport source (GetMediaInfo/CurrentURI); "x-rincon-queue:.."
                                 // = the coordinator's own queue. Only the headless button polls it.
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

  // --- Grouping ---------------------------------------------------------------------------
  // A QUEUE, not one IP per kind. The jukebox's Rooms page is a checkbox per room, so several
  // toggles can land between two netTask passes; with a single String each, the second tap
  // silently overwrote the first and that room just never joined. Drained in order, and the
  // expensive ssdpDiscover() runs ONCE after the whole batch rather than per operation — a
  // full topology fetch and parse is hundreds of ms of String-heavy work, so per-op made
  // ungrouping a four-room group visibly slow.
  struct GroupOp { String ip; bool join; };   // join=false -> BecomeCoordinatorOfStandaloneGroup
  std::vector<GroupOp> groupOps;
  bool   ungroupAll = false;  // split every member of the ACTIVE group off, in one batch

  // Per-room controls from the Rooms page. Volume targets that room's own speaker; play/pause
  // targets its group coordinator, because Sonos transport is per-group and a member cannot be
  // paused on its own (the UI only offers the button where it is honest — see screens.cpp).
  String roomVolIp;           // non-empty with roomVolTarget >= 0: set that speaker's volume
  int    roomVolTarget = -1;
  String roomPlayCoordIp;     // non-empty: play/pause this coordinator
  int    roomSetPlay   = -1;  // 0 = pause; 1 = play

  String localStreamUrl;      // non-empty: play this local HTTP file URL on the coordinator, looped
  String localStreamTitle;    // dc:title shown by Sonos for the local stream
  String playUri;             // non-empty: a fully-formed transport URI to play on the coordinator,
  String playMeta;            // with this DIDL. Used by the Radio page, where the unit already has
                              // both from the station cache and there is nothing for netTask to
                              // look up — unlike a favourite, which goes through library::.
  String wifiSsid;            // non-empty: apply these WiFi creds (with wifiPass) on netTask
  String wifiPass;
  bool   reboot = false;      // reboot the device. Set on a device-name change: a clean boot
                              // re-derives the DHCP hostname, the mDNS name and the OTA name
                              // together from the new name (see wifiHostname()/otaHostname()).
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
