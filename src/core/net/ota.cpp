#include "ota.h"
#include "wifi.h"       // wifiHostname() — the device name (NVS) else DEVICE_HOSTNAME
#include <ArduinoOTA.h>
#include <ESPmDNS.h>    // addServiceTxt() — tags our _arduino._tcp record so the portal can ID us
#include <WiFi.h>

// Optional OTA password via include/secrets.h: #define OTA_PASSWORD "..."
#include "logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "../crashlog.h" // noteReboot() — ArduinoOTA restarts the chip after onEnd returns
                         // (both kept ABOVE the secrets.h block below: inside one they would only
                         //  be defined on machines that happen to have the gitignored header)
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

static volatile bool     s_active   = false;
static volatile int      s_progress = -1;
// When the in-flight transfer last showed a sign of life (start, or a progress callback).
static volatile uint32_t s_progressMs = 0;

// s_active PARKS netTask (app.cpp's loop begins `if (otaActive()) { delay; continue; }`) and backs
// off uiTask/artTask, so a flag that gets stuck ON is indistinguishable from the field failure
// app.h documents: a device that pings, serves its config page and prints [health], while doing no
// Sonos work and never heartbeating to the portal.
//
// It is only cleared by onEnd/onError. A transfer whose client vanishes normally trips ArduinoOTA's
// own stall-reboot, but that is ArduinoOTA's guarantee, not ours, and it does not hold for every
// abort path — so anything that gets us into onStart without ever reaching onEnd/onError would
// wedge the unit until a power cycle. Bound it here instead of trusting the library.
//
// 30 s without a single progress callback is unambiguous: espota reports continuously (a ~1 MB
// image is hundreds of callbacks), so this can only fire on a genuinely dead transfer. It also
// means the netTask supervisor can never be disabled indefinitely by this flag.
static const uint32_t kOtaStallMs = 30000;

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
  ArduinoOTA.onStart([]() {
    s_active = true; s_progress = 0; s_progressMs = millis(); LOG.println("[ota] start");
  });
  // ArduinoOTA restarts the chip itself once onEnd returns, so this is the last code that runs
  // before the reset — record it here or a push-flash looks like an unexplained ESP_RST_SW.
  ArduinoOTA.onEnd([]()   { s_active = false; s_progress = -1;
                            crashlog::noteReboot("otapush");
                            LOG.println("[ota] done"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    s_progress = t ? (int)(p * 100 / t) : 0;
    s_progressMs = millis();          // proof of life for the stall guard in otaActive()
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
bool otaActive() {
  if (!s_active) return false;
  // See kOtaStallMs. Clearing from a getter is deliberate: this is read every loop by netTask,
  // uiTask and artTask, so it is the one place guaranteed to run often enough to notice, and
  // there is no other supervisor that could — the flag's whole effect is to stop those loops.
  // The writes are single-word volatiles and the transition is one-way, so a concurrent read
  // sees either the old or the new value and both are safe.
  if (millis() - s_progressMs > kOtaStallMs) {
    s_active = false;
    s_progress = -1;
    LOG.printf("[ota] no progress for %lus — clearing the active flag (was wedging netTask)\n",
               (unsigned long)(kOtaStallMs / 1000));
    return false;
  }
  return s_active;
}
int  otaProgress() { return s_progress; }
