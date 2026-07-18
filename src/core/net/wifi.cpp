#include "wifi.h"
#include "../settings.h"
#include <WiFi.h>

// Credentials come from NVS (set on-device via Settings), falling back to include/secrets.h
// (gitignored) when NVS has none — so a fresh unit connects with the dev creds, and a WiFi
// change made on the device persists and wins thereafter.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "sonos-sleep"
#endif

static volatile int s_result = WIFI_APPLY_IDLE;   // result of the last wifiApply()

// The name the router shows (DHCP hostname): the user's name from NVS, else the firmware
// default. Must be applied before WiFi.begin() to register with DHCP.
String wifiHostname() {
  String h = settingsDeviceName();
  return h.length() ? h : String(DEVICE_HOSTNAME);
}

static void applyHostname() {
  WiFi.setHostname(wifiHostname().c_str());
}

// Start a connection from the best available stored credentials (NVS first, then secrets.h).
static bool beginFromStored() {
  applyHostname();
  String ss = settingsWifiSsid(), pw = settingsWifiPass();
  if (ss.length()) { WiFi.begin(ss.c_str(), pw.c_str()); return true; }
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  return true;
#else
  return false;
#endif
}

static bool waitConnected(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

bool wifiConnect() {
  WiFi.mode(WIFI_STA);
  if (!beginFromStored()) return false;
  return waitConnected(10000);
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }

bool wifiHaveCreds() {
  if (settingsWifiSsid().length()) return true;
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  return true;
#else
  return false;
#endif
}

String wifiSsid() { return WiFi.SSID(); }

int  wifiApplyResult()      { return s_result; }
void wifiApplyResultReset() { s_result = WIFI_APPLY_IDLE; }

// Blocking — call from netTask. Try the new creds; on success persist them; on failure revert
// to the previously stored creds so the device isn't left offline.
// Reconnect from stored creds — re-applies the hostname so a name change takes effect with
// the router. Blocking; call from netTask.
void wifiReconnect() {
  WiFi.disconnect();
  delay(100);
  beginFromStored();
  waitConnected(10000);
}

void wifiApply(const String &ssid, const String &pass) {
  WiFi.disconnect();
  delay(100);
  applyHostname();
  WiFi.begin(ssid.c_str(), pass.c_str());
  if (waitConnected(12000)) {
    settingsSetWifi(ssid, pass);
    s_result = WIFI_APPLY_OK;
    return;
  }
  // Failed — restore the previous connection so we don't strand the device offline.
  WiFi.disconnect();
  delay(100);
  beginFromStored();
  waitConnected(8000);
  s_result = WIFI_APPLY_FAIL;
}
