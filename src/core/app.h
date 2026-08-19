// Core application controller — the device-agnostic Sonos control shared by every unit.
// Owns zone selection, the command/poll dispatch, and the FreeRTOS tasks (ui/net/art).
// A unit's UI never calls these directly; it communicates through g_pending / g_player
// (core/player_state.h). main.cpp calls appBoot() then appStartTasks() at startup, and
// pumps otaHandle() from loop().
#pragma once

#include <stdint.h>

void appBoot();        // WiFi + time + OTA listener + Sonos discovery + initial zone pick
void appStartTasks();  // launch the ui / net / art FreeRTOS tasks

// --- netTask liveness supervisor ---------------------------------------------------------------
// OBSERVED IN THE FIELD (2026-08-19): netTask stops iterating while every other task keeps
// running, so the device looks completely healthy and is functionally dead. Two units on
// different silicon (ESP32-S3-CAM button, ESP32-P4 jukebox) were found wedged after 4 h and 28 h
// of uptime; a third unit rebooted 10 minutes earlier was fine.
//
// How to recognise it, because "is it alive?" answers YES to every obvious probe: it pings, it
// serves its config page, its log mirror still prints [health] every few seconds, and WiFi.status()
// reads 3. What gives it away is that netTask's OWN outputs are frozen — g_linkRssi reads the
// EXACT same dBm on every sample (a live radio jitters), g_linkZones is stale, and health.soapCalls
// does not advance while uptimeSec does.
//
// Why it matters more than the dashboard tile: registrarTick() AND processPending() both live in
// netTask, so a wedged unit stops heartbeating to the portal *and* silently ignores every button
// press — the command queues into g_pending and nothing ever drains it.
//
// Every blocking call in the loop is individually bounded (soapAction 3 s connect / 4 s read with
// one retry, ssdpSeed 3x1200 ms, MDNS.queryService 3 s, httpPostJson 2 s/3 s), so the trigger is
// NOT a missing timeout in any one of them and was not identifiable from outside. Hence two
// things: `netStage` names the stage netTask is in so the next occurrence identifies itself, and
// the supervisor below turns "wedged until someone power-cycles it" into a self-heal.
//
// Call appSupervisorTick() from loop() — deliberately NOT from a new task (the sleep-machine idles
// at ~14.5 KB free heap and cannot afford another stack) and NOT from uiTick (that would need a
// change in all four units). loopTask already runs otaHandle() every ~20 ms and is independent of
// netTask, which is exactly the property required.
void        appSupervisorTick();
const char *appNetStage();      // the netTask stage as of its last iteration
uint32_t    appNetStallSec();   // seconds since netTask last iterated (0 = healthy/not started)

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
