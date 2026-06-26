#include "ota.h"
#include <ArduinoOTA.h>
#include <WiFi.h>

// Optional OTA password via include/secrets.h: #define OTA_PASSWORD "..."
#if __has_include("secrets.h")
#include "secrets.h"
#endif

static volatile bool s_active = false;
static volatile int  s_progress = -1;

void otaBegin() {
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname("sonos-nest");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([]() { s_active = true; s_progress = 0; Serial.println("[ota] start"); });
  ArduinoOTA.onEnd([]()   { s_active = false; s_progress = -1; Serial.println("[ota] done"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    s_progress = t ? (int)(p * 100 / t) : 0;
    Serial.printf("[ota] %d%%\r", s_progress);
  });
  ArduinoOTA.onError([](ota_error_t e) { s_active = false; s_progress = -1; Serial.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();
  Serial.printf("[ota] ready as sonos-nest @ %s\n", WiFi.localIP().toString().c_str());
}

void otaHandle() { ArduinoOTA.handle(); }
bool otaActive() { return s_active; }
int  otaProgress() { return s_progress; }
