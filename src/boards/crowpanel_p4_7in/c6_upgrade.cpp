// ESP32-C6 co-processor firmware upgrade probe — the jukebox's Wi-Fi lives on a C6 reached over
// SDIO (ESP-Hosted), and this board shipped with slave 2.3.0 against a 2.12.x host library. This
// is a standalone env (no core/, no LVGL). It can run with a USB cable attached and be watched, or
// be OTA'd onto a wall-mounted panel and watched over TCP (see "Wireless" below).
//
//   PLATFORMIO_BUILD_FLAGS="-DC6_PHASE=1" tools/pio run -e jukebox-c6 -t upload --upload-port /dev/ttyUSB0
//
// C6_PHASE 1: read only — host + slave versions, the update URL the core would fetch. No writes.
// C6_PHASE 2: transfer only — download, begin/write/end into the C6's INACTIVE OTA slot. No activate.
//             Phases 2 and 3 also need -DC6_IMAGE_SHA256="<hex>" (see below).
// C6_PHASE 3: transfer + activate, then restart the P4 (boot resets the C6 via C6_EN) and print the
//             version the C6 comes back with. Attempted ONCE per power cycle (RTC-retained flag),
//             so a slave that ignores activate does not get re-flashed on every boot.
//
// ⚠️ THE "host" VERSION THIS PRINTS IS NOT THE LINKED DRIVER. hostedGetHostVersion() is compiled
// against the platform package's prebuilt esp_hosted headers (2.12.11), while custom_sdkconfig links
// the one in managed_components/ (2.12.13; plans/13, 2026-09-14). So hostedHasUpdate() compares the
// wrong number, and hostedGetUpdateURL() names an image that matches the package, not the driver.
// Arduino also publishes slave images only up to 2.12.11. To match the C6 to the REAL host, build
// the slave from managed_components/espressif__esp_hosted/slave and pass it explicitly:
//
//   -DC6_IMAGE_URL="http://<build host>:8765/esp32c6-v2.12.13.bin"   plain http or https
//   -DC6_EXPECT_SLAVE="2.12.13"   skip the hostedHasUpdate() gate; done when the slave reports this
//
// Wireless: ArduinoOTA runs under DEVICE_HOSTNAME with OTA_PASSWORD, so the probe can be OTA'd in
// from the jukebox app and the app OTA'd back afterwards (`/ota`). Everything printed here is also
// served on TCP :2323 (`nc <ip> 2323`), because a wall-mounted panel has no serial console.
// The image is downloaded into PSRAM and its SHA-256 checked BEFORE the C6 is touched: inbound
// traffic is what kills this link, so the download is kept apart from the writes. A death
// mid-download leaves the C6 exactly as it was.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <stdarg.h>
#include "esp32-hal-hosted.h"
#include "esp_heap_caps.h"
#include "mbedtls/sha256.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h must define WIFI_SSID / WIFI_PASS for this probe"
#endif
// The probe runs ArduinoOTA so the app can be flashed back over it. Never unauthenticated.
#ifndef OTA_PASSWORD
#error "include/secrets.h must define OTA_PASSWORD: this probe runs ArduinoOTA and must not accept unauthenticated uploads"
#endif
#ifndef C6_PHASE
#define C6_PHASE 1
#endif
// The download runs with setInsecure() like every TLS client in this tree (no cert store on the
// device), so origin is proven by content instead: phases that WRITE require the image's SHA-256,
// taken on a trusted machine (`curl -sL <url> | sha256sum`), and refuse to touch the C6 on a mismatch.
#if C6_PHASE >= 2 && !defined(C6_IMAGE_SHA256)
#error "C6_PHASE >= 2 needs -DC6_IMAGE_SHA256=\"<64 hex chars>\" — the sha256 of the image, taken on a trusted machine"
#endif
#ifndef C6_IMAGE_SHA256
#define C6_IMAGE_SHA256 ""
#endif
#if defined(C6_IMAGE_URL) != defined(C6_EXPECT_SLAVE)
#error "C6_IMAGE_URL and C6_EXPECT_SLAVE go together: the URL bypasses hostedHasUpdate(), EXPECT says when to stop"
#endif

RTC_NOINIT_ATTR static uint32_t s_attempted;      // survives ESP.restart(), not a power cycle
static const uint32_t kAttemptedMagic = 0xC6C6C6C6;

// --- status: Serial + a PSRAM transcript served on :2323 -------------------------------------
static const size_t kLogCap = 32768;
static char *s_log = nullptr;
static size_t s_logLen = 0;
static WiFiServer s_statusServer(2323);
static WiFiClient s_statusClient;
static size_t s_statusSent = 0;

static void say(const char *fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  const size_t len = (size_t)n < sizeof line ? (size_t)n : sizeof line - 1;
  Serial.write((const uint8_t *)line, len);
  if (s_log && s_logLen + len < kLogCap) {   // full transcript or nothing past the cap
    memcpy(s_log + s_logLen, line, len);
    s_logLen += len;
  }
}

static void statusPump() {
  if (!s_statusClient || !s_statusClient.connected()) {
    WiFiClient c = s_statusServer.accept();
    if (c) { s_statusClient = c; s_statusSent = 0; }
  }
  if (s_statusClient && s_statusClient.connected() && s_statusSent < s_logLen) {
    s_statusSent += s_statusClient.write((const uint8_t *)s_log + s_statusSent, s_logLen - s_statusSent);
  }
}

static bool slaveVersion(char *out, size_t cap) {
  uint32_t sM = 0, sm = 0, sp = 0;
  hostedGetSlaveVersion(&sM, &sm, &sp);
  if (sM == 0 && sm == 0 && sp == 0) return false;   // no answer from the C6
  snprintf(out, cap, "%lu.%lu.%lu", (unsigned long)sM, (unsigned long)sm, (unsigned long)sp);
  return true;
}

static void versions(const char *when) {
  uint32_t hM, hm, hp;
  hostedGetHostVersion(&hM, &hm, &hp);
  char slave[24] = "?";
  slaveVersion(slave, sizeof slave);
  say("[c6] %s: host(pkg headers) %lu.%lu.%lu  slave %s  target %s\n", when,
      (unsigned long)hM, (unsigned long)hm, (unsigned long)hp, slave, hostedGetSlaveTargetName());
}

// Download the whole image into PSRAM and verify it. Own read loop: block, and sleep when the socket
// has nothing (CLAUDE.md on Stream helpers and the watchdog). Returns the buffer (caller frees) or
// nullptr; nothing is written to the C6 here.
static uint8_t *download(const char *url, int *outLen) {
  const bool tls = strncmp(url, "https://", 8) == 0;
  WiFiClientSecure secure;      // both clients declared before HTTPClient: it must not outlive them
  WiFiClient plain;
  if (tls) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(tls ? (WiFiClient &)secure : plain, url)) { say("[c6] http.begin failed\n"); return nullptr; }
  const int code = http.GET();
  const int len = http.getSize();
  say("[c6] GET %s -> %d, %d bytes\n", url, code, len);
  if (code != 200 || len <= 0 || len > 4 * 1024 * 1024) { http.end(); return nullptr; }
  uint8_t *img = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!img) { say("[c6] PSRAM alloc of %d failed\n", len); http.end(); return nullptr; }
  WiFiClient *st = http.getStreamPtr();
  int got = 0;
  uint32_t lastByte = millis();
  while (got < len) {
    const int avail = st->available();
    if (avail <= 0) {
      if (!st->connected() || millis() - lastByte > 20000) break;
      delay(5);
      continue;
    }
    const int n = st->read(img + got, min(avail, len - got));
    if (n <= 0) continue;
    got += n;
    lastByte = millis();
    if ((got / 4096) % 32 == 0) say("[c6] downloaded %d / %d\n", got, len);
    delay(2);                    // pace the inbound side: this link dies under sustained RX
  }
  http.end();
  say("[c6] downloaded %d of %d bytes\n", got, len);
  if (got != len) { free(img); return nullptr; }
  uint8_t digest[32];
  char hex[65];
  mbedtls_sha256(img, len, digest, 0);
  for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  say("[c6] image sha256 %s\n", hex);
  if (strcasecmp(hex, C6_IMAGE_SHA256) != 0) {
    say("[c6] SHA-256 MISMATCH (expected %s) — C6 NOT touched\n", C6_IMAGE_SHA256);
    free(img);
    return nullptr;
  }
  *outLen = len;
  return img;
}

// begin/write/end from a verified buffer. Returns true when end() succeeded.
static bool writeToSlave(const uint8_t *img, int len) {
  if (!hostedBeginUpdate()) { say("[c6] hostedBeginUpdate FAILED\n"); return false; }
  for (int off = 0; off < len; off += 4096) {
    const int n = min(4096, len - off);
    if (!hostedWriteUpdate((uint8_t *)img + off, n)) { say("[c6] hostedWriteUpdate FAILED at %d\n", off); return false; }
    if ((off / 4096) % 32 == 0) say("[c6] written %d / %d\n", off, len);
    delay(1);
  }
  const bool ended = hostedEndUpdate();
  say("[c6] hostedEndUpdate -> %s\n", ended ? "OK" : "FAILED");
  return ended;
}

static bool s_servicesStarted = false;

// Idempotent: setup() calls it when Wi-Fi is up by the end of its wait, and loop() calls it if Wi-Fi
// only connects later. Without the loop() path, a slow join left the probe with no OTA and no :2323,
// which on a wall-mounted panel means reaching for a cable.
static void otaBegin() {
  if (s_servicesStarted) return;
#ifdef DEVICE_HOSTNAME
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
#endif
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { say("[c6] OTA of the P4 starting\n"); });
  ArduinoOTA.onError([](ota_error_t e) { say("[c6] OTA error %u\n", (unsigned)e); });
  ArduinoOTA.begin();
  s_statusServer.begin();
  s_servicesStarted = true;
  say("[c6] OTA + status :2323 up at %s\n", WiFi.localIP().toString().c_str());
}

static void run() {
  if (!hostedIsInitialized()) { say("[c6] hosted NOT initialized — stopping\n"); return; }

  versions("boot");
  const bool has = hostedHasUpdate();          // also logs both versions at INFO level
  say("[c6] hostedHasUpdate=%d url=%s (both from package headers — see file header)\n", (int)has,
      hostedGetUpdateURL());
#if C6_PHASE == 1
  say("[c6] phase 1: read only, done\n");
  return;
#else
  if (WiFi.status() != WL_CONNECTED) { say("[c6] no wifi — stopping\n"); return; }
#ifdef C6_IMAGE_URL
  const char *url = C6_IMAGE_URL;
  char slave[24] = "";
  if (slaveVersion(slave, sizeof slave) && strcmp(slave, C6_EXPECT_SLAVE) == 0) {
    say("[c6] RESULT: slave reports %s == expected — nothing to do\n", slave);
    return;
  }
  say("[c6] slave %s, want %s — using explicit image %s\n", slave, C6_EXPECT_SLAVE, url);
#else
  if (!has) { say("[c6] versions match — nothing to do\n"); return; }
  const char *url = hostedGetUpdateURL();
#endif
  if (s_attempted == kAttemptedMagic) {
    say("[c6] RESULT: already attempted this power cycle and slave is not at target — NOT retrying\n");
    return;
  }
  s_attempted = kAttemptedMagic;
  int len = 0;
  uint8_t *img = download(url, &len);
  if (!img) { say("[c6] RESULT: download/verify failed — C6 unchanged\n"); return; }
  const bool ended = writeToSlave(img, len);
  free(img);
  if (!ended) { say("[c6] RESULT: write failed — inactive slot not activated, C6 unchanged\n"); return; }
#if C6_PHASE == 2
  say("[c6] phase 2: image written to the inactive slot, NOT activated, done\n");
  versions("after end");
  return;
#else
  const bool act = hostedActivateUpdate();
  say("[c6] hostedActivateUpdate -> %s\n", act ? "OK" : "FAILED (expected on a pre-2.6 slave)");
  versions("after activate");
  say("[c6] restarting the P4 in 3 s — boot resets the C6; watch the 'boot' version line\n");
  statusPump();
  delay(3000);
  ESP.restart();
#endif
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);
  s_log = (char *)heap_caps_malloc(kLogCap, MALLOC_CAP_SPIRAM);
  say("\n[c6] probe phase %d, reset reason %d, attempted-flag %s\n", C6_PHASE, (int)esp_reset_reason(),
      s_attempted == kAttemptedMagic ? "SET" : "clear");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t deadline = millis() + 30000;
  while (WiFi.status() != WL_CONNECTED && (int32_t)(deadline - millis()) > 0) delay(200);
  say("[c6] wifi status %d ip %s rssi %d\n", (int)WiFi.status(), WiFi.localIP().toString().c_str(),
      (int)WiFi.RSSI());
  if (WiFi.status() == WL_CONNECTED) otaBegin();
  run();
  say("[c6] setup done — idling with OTA + status up\n");
}

void loop() {
  if (!s_servicesStarted) {
    if (WiFi.status() != WL_CONNECTED) { delay(200); return; }
    otaBegin();
  }
  ArduinoOTA.handle();
  statusPump();
  static uint32_t last = 0;
  if (millis() - last > 30000) {
    last = millis();
    versions("idle");
  }
  delay(20);
}
