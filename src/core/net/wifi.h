// WiFi connection + credential persistence (NVS, falling back to include/secrets.h).
#pragma once

#include <Arduino.h>

bool wifiConnect();       // connect from stored creds (NVS -> secrets.h)
bool wifiIsConnected();
String wifiSsid();        // currently-associated SSID ("" if not connected)

// Runtime WiFi change (Settings -> Wi-Fi). The UI posts new creds; netTask runs wifiApply()
// (blocking): it tries them, persists on success, and reverts to the previous creds on
// failure. The UI polls wifiApplyResult() for the outcome.
enum { WIFI_APPLY_IDLE = 0, WIFI_APPLY_OK = 1, WIFI_APPLY_FAIL = 2 };
void wifiApply(const String &ssid, const String &pass);
int  wifiApplyResult();
void wifiApplyResultReset();
