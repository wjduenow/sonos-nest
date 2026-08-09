// Core application controller — the device-agnostic Sonos control shared by every unit.
// Owns zone selection, the command/poll dispatch, and the FreeRTOS tasks (ui/net/art).
// A unit's UI never calls these directly; it communicates through g_pending / g_player
// (core/player_state.h). main.cpp calls appBoot() then appStartTasks() at startup, and
// pumps otaHandle() from loop().
#pragma once

#include <stdint.h>

void appBoot();        // WiFi + time + OTA listener + Sonos discovery + initial zone pick
void appStartTasks();  // launch the ui / net / art FreeRTOS tasks

// --- Link snapshot, published by netTask, for UI-side diagnostics ------------------------------
// WiFi.RSSI() / WiFi.localIP() are not free on every board: where the radio is a separate
// co-processor (jukebox = ESP32-P4 host + ESP32-C6 over SDIO/ESP-Hosted) they are BLOCKING RPCs.
// Calling them from the UI task stalls rendering for the RPC timeout exactly when the transport is
// dying — i.e. a health log that induces the freeze it exists to report. Same for sonos::zones(),
// which netTask owns and rewrites during discovery.
//
// netTask is already the network side, so it caches all three here and the UI prints the copy.
// Plain 32-bit scalars: single-word writes, so no tearing and no lock. The IP is packed
// little-endian by octet (a[0] in the low byte) rather than stored as a String — a String write
// is not atomic and this is read from another task.
extern volatile int      g_linkStatus;   // WiFi.status()
extern volatile int      g_linkRssi;     // dBm; 0 while "connected" = the radio RPC is dead
extern volatile uint32_t g_linkIp;       // IPv4, octet 0 in the low byte
extern volatile uint32_t g_linkZones;    // sonos::zones().size() as of the last poll
