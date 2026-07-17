// Persistent settings in NVS (flash) via Preferences. Survives reboot/power loss.
#pragma once

#include <Arduino.h>

void    settingsInit();
String  settingsRoom();                  // last-selected room name, or "" if unset
void    settingsSetRoom(const String &name);

uint8_t settingsBrightness();            // backlight % (10..100), default 100
void    settingsSetBrightness(uint8_t pct);

// Button-ring level % (0..100), default 100. Deliberately NOT settingsBrightness(): that one
// floors at 10 so nobody can blank an LCD and lose the UI needed to un-blank it. A ring has no
// such trap and 0 (fully off) is a legitimate, wanted state on a bedside device.
uint8_t settingsRing();
void    settingsSetRing(uint8_t pct);

String  settingsZones();                 // cached discovered-zone blob ("" if none)
void    settingsSetZones(const String &blob);

String  settingsWifiSsid();              // saved WiFi creds (NVS); "" if never set on-device
String  settingsWifiPass();
void    settingsSetWifi(const String &ssid, const String &pass);

String  settingsSleepTrack();            // selected local sleep-track path ("" = unit default)
void    settingsSetSleepTrack(const String &path);

String  settingsWakeTrack();             // selected local wake-track path ("" = auto: /Wake.mp3)
void    settingsSetWakeTrack(const String &path);

String  settingsDeviceName();            // network/DHCP hostname ("" = firmware default)
void    settingsSetDeviceName(const String &name);
