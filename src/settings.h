// Persistent settings in NVS (flash) via Preferences. Survives reboot/power loss.
#pragma once

#include <Arduino.h>

void   settingsInit();
String settingsRoom();                  // last-selected room name, or "" if unset
void   settingsSetRoom(const String &name);
