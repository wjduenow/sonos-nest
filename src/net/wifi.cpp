#include "wifi.h"
#include <WiFi.h>
#include <Preferences.h>

// TODO Phase 1: load SSID/pass from NVS (or include/secrets.h during dev), connect.
// TODO Phase 4: captive portal when no creds; persist on success.

bool wifiConnect() {
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
  return WiFi.status() == WL_CONNECTED;
#else
  return false;  // TODO: NVS creds / captive portal
#endif
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
