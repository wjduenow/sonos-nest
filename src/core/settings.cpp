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

uint8_t settingsBrightness() {
  uint8_t b = s_prefs.getUChar("bright", 100);
  return b < 10 ? 10 : b;
}

void settingsSetBrightness(uint8_t pct) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  s_prefs.putUChar("bright", pct);
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
