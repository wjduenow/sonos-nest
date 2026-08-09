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
// UI feedback tone level, 0..100. 0 = off. Boards without a speaker ignore it.
uint8_t settingsUiSound();
void    settingsSetUiSound(uint8_t pct);

// Scroll/detent feedback, separate from the master level above: wanting button clicks without
// scroll noise is a reasonable preference, and a 50-row flick makes far more sound than a button.
// settingsUiSound()==0 silences everything regardless — this only has meaning above that.
bool    settingsScrollSound();
void    settingsSetScrollSound(bool on);

// --- Radio cache refresh schedule ---------------------------------------------------------------
// The station catalogue is crawled once a day at a fixed LOCAL hour (the device's CLOCK_TZ), not on
// an age timer: a predictable overnight slot keeps ~500 KB of traffic off the ESP-Hosted link at the
// times anyone is listening. Default 4 (04:00 local).
bool    settingsRadioAutoRefresh();
void    settingsSetRadioAutoRefresh(bool on);
uint8_t settingsRadioRefreshHour();          // 0-23 local
void    settingsSetRadioRefreshHour(uint8_t hour);

// --- Amazon Music (SMAPI DeviceLink) ------------------------------------------------------------
// Our OWN account credentials, not the household's — Sonos never discloses those (plans/08). The
// token expires in under an hour and is refreshed in-band by core/amazon.cpp, so these are written
// far more often than most settings; both are ~600 chars.
String  settingsAmazonToken();
String  settingsAmazonKey();
void    settingsSetAmazonAuth(const String &token, const String &key);
// Account serial for the sn= URI parameter. Playback ignores it in practice, but it costs nothing
// to send the right one, and it is readable from any existing Amazon favourite's res URI.
uint8_t settingsAmazonSerial();
void    settingsSetAmazonSerial(uint8_t sn);
// The Sonos household id, used as the correlation key when linking.
String  settingsHouseholdId();
void    settingsSetHouseholdId(const String &id);

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

// Last-known sonos-portal address as "ip:port" ("" if never resolved). Cached from the mDNS
// query so a later boot can self-register before a fresh query resolves — see net/registrar.cpp.
String  settingsPortal();
void    settingsSetPortal(const String &hostPort);

// --- OTA pull-update (net/updater.cpp; plans/06). Both opt-in, default off/empty. ---
bool    settingsOtaAuto();               // auto-apply published updates (at boot)? default false
void    settingsSetOtaAuto(bool on);

String  settingsUpdateUrl();             // firmware manifest URL; "" (default) = pull disabled,
void    settingsSetUpdateUrl(const String &url);   // espota-only. Portal (LAN http) or GitHub (https).

// --- sonos-button ---
String  settingsPlaylist();              // Sonos saved-playlist name to start, default "Sleep"
void    settingsSetPlaylist(const String &name);

uint8_t settingsVolume();                // fixed volume the button plays at (0..100), default 30
void    settingsSetVolume(uint8_t pct);
