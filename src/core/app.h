// Core application controller — the device-agnostic Sonos control shared by every unit.
// Owns zone selection, the command/poll dispatch, and the FreeRTOS tasks (ui/net/art).
// A unit's UI never calls these directly; it communicates through g_pending / g_player
// (core/player_state.h). main.cpp calls appBoot() then appStartTasks() at startup, and
// pumps otaHandle() from loop().
#pragma once

void appBoot();        // WiFi + time + OTA listener + Sonos discovery + initial zone pick
void appStartTasks();  // launch the ui / net / art FreeRTOS tasks
