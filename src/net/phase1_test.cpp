#include "phase1_test.h"
#include <Arduino.h>
#include <WiFi.h>

#include "wifi.h"
#include "../hw/pcf8574.h"
#include "../hw/encoder.h"
#include "../sonos/ssdp.h"
#include "../sonos/soap_client.h"

static const char *transportName(TransportState s) {
  switch (s) {
    case TransportState::Playing:       return "PLAYING";
    case TransportState::Paused:        return "PAUSED";
    case TransportState::Stopped:       return "STOPPED";
    case TransportState::Transitioning: return "TRANSITIONING";
    default:                            return "UNKNOWN";
  }
}

void phase1Run() {
  Serial.println("\n===== Phase 1: Sonos SOAP test =====");

  // Inputs (button via expander, encoder via PCNT).
  pcf8574Init();
  encoderInit();

  // WiFi.
  Serial.printf("[phase1] connecting WiFi...\n");
  if (!wifiConnect()) {
    Serial.println("[phase1] WiFi FAILED — check include/secrets.h");
    for (;;) delay(1000);
  }
  Serial.printf("[phase1] WiFi OK, IP=%s\n", WiFi.localIP().toString().c_str());

  // Discover Sonos.
  Serial.println("[phase1] SSDP discovering Sonos ZonePlayers...");
  sonos::ssdpDiscover();
  const std::vector<sonos::Zone> &zs = sonos::zones();
  Serial.printf("[phase1] found %u zone(s):\n", (unsigned)zs.size());
  for (const auto &z : zs)
    Serial.printf("   - %-18s @ %-15s  %s\n", z.name.c_str(), z.ip.c_str(), z.uuid.c_str());
  if (zs.empty()) {
    Serial.println("[phase1] no Sonos found. Check the WiFi/VLAN, or set -DSONOS_ZONE_IP.");
    for (;;) delay(1000);
  }

  String ip = zs[0].ip;
  Serial.printf("[phase1] controlling: %s @ %s\n", zs[0].name.c_str(), ip.c_str());
  Serial.println("[phase1] TWIST = volume   PRESS = play/pause");

  // Prime current volume + state.
  uint8_t vol = 0;
  sonos::getVolume(ip, vol);
  TransportState st = TransportState::Unknown;
  sonos::getTransportInfo(ip, st);
  Serial.printf("[phase1] start: %s  vol=%u\n", transportName(st), vol);

  uint32_t lastPoll = 0;
  for (;;) {
    int32_t d = encoderDelta();
    if (d != 0) {
      int v = (int)vol + d;
      v = v < 0 ? 0 : (v > 100 ? 100 : v);
      vol = (uint8_t)v;
      if (sonos::setVolume(ip, vol)) Serial.printf("[vol] %u\n", vol);
      else                           Serial.println("[vol] setVolume FAILED");
    }

    if (knobPressed()) {
      if (sonos::getTransportInfo(ip, st)) {
        if (st == TransportState::Playing) {
          Serial.println(sonos::pause(ip) ? "[transport] -> PAUSE" : "[transport] pause FAILED");
        } else {
          Serial.println(sonos::play(ip) ? "[transport] -> PLAY" : "[transport] play FAILED");
        }
      }
    }

    if (millis() - lastPoll > 3000) {
      lastPoll = millis();
      sonos::getTransportInfo(ip, st);
      sonos::getVolume(ip, vol);
      Serial.printf("[status] %s  vol=%u\n", transportName(st), vol);
    }
    delay(5);
  }
}
