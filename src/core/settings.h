// Persistent settings in NVS (flash) via Preferences. Survives reboot/power loss.
#pragma once

#include <Arduino.h>

void    settingsInit();
String  settingsRoom();                  // last-selected room name, or "" if unset
void    settingsSetRoom(const String &name);

uint8_t settingsBrightness();            // backlight % (10..100), default 100
void    settingsSetBrightness(uint8_t pct);

String  settingsZones();                 // cached discovered-zone blob ("" if none)
void    settingsSetZones(const String &blob);

String  settingsWifiSsid();              // saved WiFi creds (NVS); "" if never set on-device
String  settingsWifiPass();
void    settingsSetWifi(const String &ssid, const String &pass);

String  settingsSleepTrack();            // selected local sleep-track path ("" = unit default)
void    settingsSetSleepTrack(const String &path);

String  settingsDeviceName();            // network/DHCP hostname ("" = firmware default)
void    settingsSetDeviceName(const String &name);
