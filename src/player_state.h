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

// Guard every read/write of the global PlayerState with this mutex.
extern SemaphoreHandle_t g_stateMutex;
extern PlayerState       g_player;

void playerStateInit();
