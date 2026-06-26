#include "ota.h"
#include <ArduinoOTA.h>
#include <WiFi.h>

// Optional OTA password via include/secrets.h: #define OTA_PASSWORD "..."
#if __has_include("secrets.h")
#include "secrets.h"
#endif

static volatile bool s_active = false;

void otaBegin() {
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname("sonos-nest");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([]() { s_active = true; Serial.println("[ota] start"); });
  ArduinoOTA.onEnd([]()   { s_active = false; Serial.println("[ota] done"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    Serial.printf("[ota] %u%%\r", t ? p * 100 / t : 0);
  });
  ArduinoOTA.onError([](ota_error_t e) { s_active = false; Serial.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();
  Serial.printf("[ota] ready as sonos-nest @ %s\n", WiFi.localIP().toString().c_str());
}

void otaHandle() { ArduinoOTA.handle(); }
bool otaActive() { return s_active; }
