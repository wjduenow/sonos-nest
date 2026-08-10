#include "ota.h"
#include "wifi.h"       // wifiHostname() — the device name (NVS) else DEVICE_HOSTNAME
#include <ArduinoOTA.h>
#include <ESPmDNS.h>    // addServiceTxt() — tags our _arduino._tcp record so the portal can ID us
#include <WiFi.h>

// Optional OTA password via include/secrets.h: #define OTA_PASSWORD "..."
#include "logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
// NB above the secrets conditional on purpose: inside it, LOG would only be defined on
// machines that happen to have include/secrets.h, which is gitignored.

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Per-unit mDNS/OTA name; set by the build env (-DDEVICE_HOSTNAME). Two units on one LAN
// must differ or they collide on <name>.local.
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "sonos-nest"
#endif

static volatile bool s_active = false;
static volatile int  s_progress = -1;

// The mDNS/OTA name follows the device name (settingsDeviceName, else the DEVICE_HOSTNAME
// default) — same source as the DHCP hostname — so two of the same unit on one LAN don't both
// claim <default>.local. Latched at otaBegin(); a runtime rename reboots to re-read it. Returns
// a pointer into a persistent buffer that ArduinoOTA/callers copy immediately.
const char *otaHostname() {
  static String h;
  h = wifiHostname();
  return h.c_str();
}

void otaBegin() {
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname(otaHostname());
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([]() { s_active = true; s_progress = 0; LOG.println("[ota] start"); });
  ArduinoOTA.onEnd([]()   { s_active = false; s_progress = -1; LOG.println("[ota] done"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    s_progress = t ? (int)(p * 100 / t) : 0;
    LOG.printf("[ota] %d%%\r", s_progress);
  });
  ArduinoOTA.onError([](ota_error_t e) { s_active = false; s_progress = -1; LOG.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();
  // ArduinoOTA advertises a generic _arduino._tcp; every ESP32 running ArduinoOTA does. Tag ours
  // with a compiled-in marker (NOT the user-settable hostname) so the portal's mDNS discovery
  // fallback can tell a sonos-nest-project device from any other board on the LAN. See
  // sonos-portal/app/mdns.py — devices without this TXT key are ignored.
  MDNS.addServiceTxt("arduino", "tcp", "app", "sonos-nest");
  LOG.printf("[ota] ready as %s @ %s\n", otaHostname(), WiFi.localIP().toString().c_str());
}

void otaHandle() { ArduinoOTA.handle(); }
bool otaActive() { return s_active; }
int  otaProgress() { return s_progress; }
