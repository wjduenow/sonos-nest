// WiFi connection + credential persistence (NVS, falling back to include/secrets.h).
#pragma once

#include <Arduino.h>

bool wifiConnect();       // connect from stored creds (NVS -> secrets.h)
bool wifiIsConnected();

// True if there are ANY credentials to try — NVS creds set on-device, or compile-time
// WIFI_SSID/WIFI_PASS from secrets.h. False only on a genuinely-unprovisioned unit, which is
// the one case the captive portal opens unprompted (a merely-failed connect must not).
bool wifiHaveCreds();
String wifiSsid();        // currently-associated SSID ("" if not connected)

// The DHCP hostname actually registered with the router: the user's name from NVS, else the
// firmware default (DEVICE_HOSTNAME). Single source of truth — wifi.cpp is what sets it, so
// anything that wants to *display* the effective name asks here rather than re-deriving the
// "stored, else default" rule and drifting from it.
String wifiHostname();

// Runtime WiFi change (Settings -> Wi-Fi). The UI posts new creds; netTask runs wifiApply()
// (blocking): it tries them, persists on success, and reverts to the previous creds on
// failure. The UI polls wifiApplyResult() for the outcome.
enum { WIFI_APPLY_IDLE = 0, WIFI_APPLY_OK = 1, WIFI_APPLY_FAIL = 2 };
void wifiApply(const String &ssid, const String &pass);
int  wifiApplyResult();
void wifiApplyResultReset();
void wifiReconnect();     // reconnect from stored creds (re-applies the hostname)
