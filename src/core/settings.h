// Persistent settings in NVS (flash) via Preferences. Survives reboot/power loss.
#pragma once

#include <Arduino.h>

void    settingsInit();
String  settingsRoom();                  // last-selected room name, or "" if unset
void    settingsSetRoom(const String &name);

uint8_t settingsBrightness();            // backlight % (10..100), default 100
void    settingsSetBrightness(uint8_t pct);

// --- Screensaver (screened units; sonos-jukebox implements it) -----------------------------------
// Three settings, because they answer three different questions and one number cannot:
// what to show, when to show it, and when to give up and turn the panel off entirely.
//
// The panel is an IPS LCD, so the risk here is reversible image RETENTION, not the permanent
// emitter wear an OLED suffers — which is why the backlight settings matter more than the picture.
// The unit owns the state machine (a board must not read settings), and applies all three through
// backlightSet(); see the screensaver block in units/sonos_jukebox/screens.cpp.
enum SaverMode : uint8_t {
  SAVER_OFF   = 0,   // never; the UI stays up (the blank timer still applies)
  SAVER_CLOCK = 1,   // clock + date on near-black
  SAVER_COVER = 2,   // clock over the current album art, always
  SAVER_AUTO  = 3,   // cover while something is playing with art, else clock  (default)
};
uint8_t settingsSaverMode();
void    settingsSetSaverMode(uint8_t mode);

// Idle seconds before the screensaver appears. 0 = never show it. Default 120.
uint16_t settingsSaverDelaySec();
void     settingsSetSaverDelaySec(uint16_t sec);

// Backlight % while the screensaver is up. Floored at 5 — 0 belongs to the blank timer below, and
// a screensaver you cannot see is just a confusing way to spell "off". Default 40.
uint8_t settingsSaverDimPct();
void    settingsSetSaverDimPct(uint8_t pct);

// Idle MINUTES before the backlight goes fully off. 0 = never. Default 60. Touch still wakes the
// panel with the backlight at zero (the touch controller is a separate I2C device), so this is
// safe to set aggressively.
uint16_t settingsSaverBlankMin();
void     settingsSetSaverBlankMin(uint16_t min);

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

// Favourites get their OWN schedule rather than sharing the radio one. They are cheap and change
// often (anything edited in the Sonos app), where the Amazon station crawl is expensive and
// changes rarely — so the useful cadences differ. Defaults to 05:00, an hour after the radio
// default, deliberately: running both heavy refreshes in the same hour is what this board's
// internal SRAM cannot afford (see kMinHeap in fav_cache.cpp).
bool    settingsFavAutoRefresh();
void    settingsSetFavAutoRefresh(bool on);
uint8_t settingsFavRefreshHour();            // 0-23 local
void    settingsSetFavRefreshHour(uint8_t hour);

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
