#include "settings.h"
#include <Preferences.h>

static Preferences s_prefs;

void settingsInit() {
  s_prefs.begin("sonos", false);  // namespace "sonos", read/write
}

String settingsRoom() {
  return s_prefs.getString("room", "");
}

void settingsSetRoom(const String &name) {
  if (settingsRoom() != name) s_prefs.putString("room", name);
}

// UI feedback tone level. Defaults to a modest 40: audible confirmation without being a novelty
// the owner immediately wants to switch off. 0 disables it entirely.
uint8_t settingsUiSound() { return s_prefs.getUChar("uisnd", 40); }
void settingsSetUiSound(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_prefs.putUChar("uisnd", pct);
}

bool settingsScrollSound()           { return s_prefs.getUChar("scrsnd", 1) != 0; }
void settingsSetScrollSound(bool on) { s_prefs.putUChar("scrsnd", on ? 1 : 0); }

bool settingsRadioAutoRefresh()           { return s_prefs.getUChar("rdauto", 1) != 0; }
void settingsSetRadioAutoRefresh(bool on) { s_prefs.putUChar("rdauto", on ? 1 : 0); }
uint8_t settingsRadioRefreshHour()        { uint8_t h = s_prefs.getUChar("rdhour", 4); return h > 23 ? 4 : h; }
void settingsSetRadioRefreshHour(uint8_t hour) { s_prefs.putUChar("rdhour", hour > 23 ? 4 : hour); }

// NVS keys are capped at 15 chars; "fvauto"/"fvhour" match the "rd*" pair above.
bool settingsFavAutoRefresh()             { return s_prefs.getUChar("fvauto", 1) != 0; }
void settingsSetFavAutoRefresh(bool on)   { s_prefs.putUChar("fvauto", on ? 1 : 0); }
uint8_t settingsFavRefreshHour()          { uint8_t h = s_prefs.getUChar("fvhour", 5); return h > 23 ? 5 : h; }
void settingsSetFavRefreshHour(uint8_t hour) { s_prefs.putUChar("fvhour", hour > 23 ? 5 : hour); }

// Amazon Music DeviceLink credentials. NVS keys are capped at 15 chars.
String settingsAmazonToken() { return s_prefs.getString("amztok", ""); }
String settingsAmazonKey()   { return s_prefs.getString("amzkey", ""); }
void settingsSetAmazonAuth(const String &token, const String &key) {
  s_prefs.putString("amztok", token);
  s_prefs.putString("amzkey", key);
}
uint8_t settingsAmazonSerial()          { return s_prefs.getUChar("amzsn", 0); }
void    settingsSetAmazonSerial(uint8_t sn) { s_prefs.putUChar("amzsn", sn); }
String  settingsHouseholdId()           { return s_prefs.getString("hhid", ""); }
void    settingsSetHouseholdId(const String &id) {
  if (settingsHouseholdId() != id) s_prefs.putString("hhid", id);
}

uint8_t settingsBrightness() {
  uint8_t b = s_prefs.getUChar("bright", 100);
  return b < 10 ? 10 : b;
}

void settingsSetBrightness(uint8_t pct) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  s_prefs.putUChar("bright", pct);
}

// No 10% floor here, unlike settingsBrightness() above — see settings.h. 0 means "ring off",
// which is a state a bedside device genuinely wants.
uint8_t settingsRing() {
  uint8_t r = s_prefs.getUChar("ring", 100);
  return r > 100 ? 100 : r;
}

void settingsSetRing(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_prefs.putUChar("ring", pct);
}

// --- sonos-button ---
// "Sleep" matches the playlist the sleep-machine's Bedtime button already starts, so a fresh
// device does the right thing before anyone opens the config page.
String settingsPlaylist() { return s_prefs.getString("playlist", "Sleep"); }
void   settingsSetPlaylist(const String &name) { s_prefs.putString("playlist", name); }

uint8_t settingsVolume() {
  uint8_t v = s_prefs.getUChar("btnvol", 30);
  return v > 100 ? 100 : v;
}
void settingsSetVolume(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_prefs.putUChar("btnvol", pct);
}

String settingsZones() { return s_prefs.getString("zones", ""); }

void settingsSetZones(const String &blob) { s_prefs.putString("zones", blob); }

String settingsWifiSsid() { return s_prefs.getString("wifi_ssid", ""); }
String settingsWifiPass() { return s_prefs.getString("wifi_pass", ""); }

void settingsSetWifi(const String &ssid, const String &pass) {
  s_prefs.putString("wifi_ssid", ssid);
  s_prefs.putString("wifi_pass", pass);
}

String settingsSleepTrack() { return s_prefs.getString("track", ""); }

void settingsSetSleepTrack(const String &path) { s_prefs.putString("track", path); }

String settingsWakeTrack() { return s_prefs.getString("waketrack", ""); }

void settingsSetWakeTrack(const String &path) { s_prefs.putString("waketrack", path); }

String settingsDeviceName() { return s_prefs.getString("devname", ""); }

void settingsSetDeviceName(const String &name) { s_prefs.putString("devname", name); }

String settingsPortal() { return s_prefs.getString("portal", ""); }

void settingsSetPortal(const String &hostPort) {
  if (settingsPortal() != hostPort) s_prefs.putString("portal", hostPort);
}

bool settingsOtaAuto() { return s_prefs.getBool("otaauto", false); }
void settingsSetOtaAuto(bool on) { s_prefs.putBool("otaauto", on); }

String settingsUpdateUrl() { return s_prefs.getString("updurl", ""); }
void   settingsSetUpdateUrl(const String &url) {
  if (settingsUpdateUrl() != url) s_prefs.putString("updurl", url);
}
