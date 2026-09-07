// ESP32-C6 co-processor firmware upgrade probe — the jukebox's Wi-Fi lives on a C6 reached over
// SDIO (ESP-Hosted), and this board shipped with slave 2.3.0 against a 2.12.x host library. This
// is a standalone env (no core/, no LVGL) so it can run with a USB cable attached and be watched.
//
//   PLATFORMIO_BUILD_FLAGS="-DC6_PHASE=1" tools/pio run -e jukebox-c6 -t upload --upload-port /dev/ttyUSB0
//
// C6_PHASE 1: read only — host + slave versions, the update URL the core would fetch. No writes.
// C6_PHASE 2: transfer only — download, begin/write/end into the C6's INACTIVE OTA slot. No activate.
//             Phases 2 and 3 also need -DC6_IMAGE_SHA256="<hex>" (see below).
// C6_PHASE 3: transfer + activate, then restart the P4 (boot resets the C6 via C6_EN) and print the
//             version the C6 comes back with. Attempted ONCE per power cycle (RTC-retained flag),
//             so a slave that ignores activate does not get re-flashed on every boot.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp32-hal-hosted.h"
#include "mbedtls/sha256.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h must define WIFI_SSID / WIFI_PASS for this probe"
#endif
#ifndef C6_PHASE
#define C6_PHASE 1
#endif
// The download runs with setInsecure() like every TLS client in this tree (no cert store on the
// device), so origin is proven by content instead: phases that WRITE require the image's SHA-256,
// taken on a trusted machine (`curl -sL <url> | sha256sum`), and refuse to end() on a mismatch.
#if C6_PHASE >= 2 && !defined(C6_IMAGE_SHA256)
#error "C6_PHASE >= 2 needs -DC6_IMAGE_SHA256=\"<64 hex chars>\" — the sha256 of the image, taken on a trusted machine"
#endif
#ifndef C6_IMAGE_SHA256
#define C6_IMAGE_SHA256 ""
#endif

RTC_NOINIT_ATTR static uint32_t s_attempted;      // survives ESP.restart(), not a power cycle
static const uint32_t kAttemptedMagic = 0xC6C6C6C6;

static void versions(const char *when) {
  uint32_t hM, hm, hp, sM, sm, sp;
  hostedGetHostVersion(&hM, &hm, &hp);
  hostedGetSlaveVersion(&sM, &sm, &sp);
  Serial.printf("[c6] %s: host %lu.%lu.%lu  slave %lu.%lu.%lu  target %s\n", when,
                (unsigned long)hM, (unsigned long)hm, (unsigned long)hp,
                (unsigned long)sM, (unsigned long)sm, (unsigned long)sp, hostedGetSlaveTargetName());
}

// Download + begin/write/end. Returns true when end() succeeded. Own read loop: block, and sleep
// when the socket has nothing (CLAUDE.md on Stream helpers and the watchdog).
static bool transfer(const char *url) {
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(cli, url)) { Serial.println("[c6] http.begin failed"); return false; }
  const int code = http.GET();
  const int len = http.getSize();
  Serial.printf("[c6] GET %s -> %d, %d bytes\n", url, code, len);
  if (code != 200 || len <= 0) { http.end(); return false; }
  if (!hostedBeginUpdate()) { Serial.println("[c6] hostedBeginUpdate FAILED"); http.end(); return false; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient *st = http.getStreamPtr();
  static uint8_t buf[4096];
  int got = 0;
  const uint32_t deadline = millis() + 120000;
  while (got < len && (int32_t)(deadline - millis()) > 0) {
    const int avail = st->available();
    if (avail <= 0) { if (!st->connected()) break; delay(5); continue; }
    const int n = st->read(buf, min(avail, (int)sizeof buf));
    if (n <= 0) continue;
    mbedtls_sha256_update(&sha, buf, n);
    if (!hostedWriteUpdate(buf, n)) { Serial.printf("[c6] hostedWriteUpdate FAILED at %d\n", got); http.end(); return false; }
    got += n;
    if ((got / 4096) % 16 == 0) Serial.printf("[c6] %d / %d\n", got, len);
    delay(1);
  }
  http.end();
  Serial.printf("[c6] transferred %d of %d bytes\n", got, len);
  if (got != len) return false;
  uint8_t digest[32]; char hex[65];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  Serial.printf("[c6] image sha256 %s\n", hex);
  if (strcasecmp(hex, C6_IMAGE_SHA256) != 0) {
    Serial.printf("[c6] SHA-256 MISMATCH (expected %s) — NOT ending the update; the inactive slot is left unactivated\n", C6_IMAGE_SHA256);
    return false;
  }
  const bool ended = hostedEndUpdate();
  Serial.printf("[c6] hostedEndUpdate -> %s\n", ended ? "OK" : "FAILED");
  return ended;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n[c6] probe phase %d, reset reason %d\n", C6_PHASE, (int)esp_reset_reason());
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t deadline = millis() + 30000;
  while (WiFi.status() != WL_CONNECTED && (int32_t)(deadline - millis()) > 0) delay(200);
  Serial.printf("[c6] wifi status %d ip %s rssi %d\n", (int)WiFi.status(),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  if (!hostedIsInitialized()) { Serial.println("[c6] hosted NOT initialized — stopping"); return; }

  versions("boot");
  const bool has = hostedHasUpdate();          // also logs both versions at INFO level
  Serial.printf("[c6] hostedHasUpdate=%d url=%s\n", (int)has, hostedGetUpdateURL());
#if C6_PHASE == 1
  Serial.println("[c6] phase 1: read only, done");
  return;
#else
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[c6] no wifi — stopping"); return; }
  if (!has) { Serial.println("[c6] versions match — nothing to do"); return; }
  if (s_attempted == kAttemptedMagic) { Serial.println("[c6] already attempted this power cycle — NOT retrying"); return; }
  s_attempted = kAttemptedMagic;
  if (!transfer(hostedGetUpdateURL())) { Serial.println("[c6] transfer failed — C6 unchanged"); return; }
#if C6_PHASE == 2
  Serial.println("[c6] phase 2: image written to the inactive slot, NOT activated, done");
  versions("after end");
  return;
#else
  const bool act = hostedActivateUpdate();
  Serial.printf("[c6] hostedActivateUpdate -> %s\n", act ? "OK" : "FAILED (expected on a pre-2.6 slave)");
  versions("after activate");
  Serial.println("[c6] restarting the P4 in 3 s — boot resets the C6; watch the 'boot' version line");
  delay(3000);
  ESP.restart();
#endif
#endif
}

void loop() { delay(5000); Serial.println("[c6] idle"); }
