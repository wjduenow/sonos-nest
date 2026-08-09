#include "core/net/logmirror.h"

#ifdef LOG_MIRROR

#include <WiFi.h>
#include "core/net/wifi.h"

namespace {

constexpr size_t kRing       = 8192;   // ~80 heartbeat lines of slack
constexpr int    kMaxClients = 2;
constexpr size_t kChunk      = 512;

uint8_t       *s_ring = nullptr;
volatile size_t s_head = 0, s_tail = 0;
portMUX_TYPE   s_mux  = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t s_dropped = 0;

WiFiServer *s_srv = nullptr;
WiFiClient  s_cli[kMaxClients];
volatile int s_nclients = 0;
uint16_t     s_port = 2323;

// Copy into the ring, dropping the OLDEST bytes on overflow. Short critical section:
// a full heartbeat line is ~120 bytes, so this is a couple of microseconds.
void ringPush(const uint8_t *b, size_t n) {
  if (!s_ring) return;
  portENTER_CRITICAL(&s_mux);
  for (size_t i = 0; i < n; ++i) {
    size_t next = (s_head + 1) % kRing;
    if (next == s_tail) {                 // full — discard the oldest byte
      s_tail = (s_tail + 1) % kRing;
      s_dropped++;
    }
    s_ring[s_head] = b[i];
    s_head = next;
  }
  portEXIT_CRITICAL(&s_mux);
}

size_t ringPop(uint8_t *out, size_t max) {
  size_t n = 0;
  portENTER_CRITICAL(&s_mux);
  while (n < max && s_tail != s_head) {
    out[n++] = s_ring[s_tail];
    s_tail = (s_tail + 1) % kRing;
  }
  portEXIT_CRITICAL(&s_mux);
  return n;
}

void logTask(void *) {
  // appBoot() brings WiFi up well after uiInit() calls us, so wait for it here rather
  // than making the caller sequence it.
  while (!wifiIsConnected()) vTaskDelay(pdMS_TO_TICKS(500));

  s_srv = new WiFiServer(s_port);
  s_srv->begin();
  s_srv->setNoDelay(true);
  Serial.printf("[logmirror] listening on %s:%u\n",
                WiFi.localIP().toString().c_str(), s_port);

  uint8_t buf[kChunk];
  for (;;) {
    // --- accept -------------------------------------------------------------
    WiFiClient incoming = s_srv->accept();
    if (incoming) {
      int slot = -1;
      for (int i = 0; i < kMaxClients; ++i)
        if (!s_cli[i] || !s_cli[i].connected()) { slot = i; break; }
      if (slot < 0) {
        incoming.println("[logmirror] busy");
        incoming.stop();
      } else {
        s_cli[slot] = incoming;
        s_cli[slot].setNoDelay(true);
        s_cli[slot].printf("[logmirror] attached to " DEVICE_HOSTNAME
                           " — %u dropped byte(s) so far\n", (unsigned)s_dropped);
      }
    }

    // --- reap ---------------------------------------------------------------
    int live = 0;
    for (int i = 0; i < kMaxClients; ++i) {
      if (s_cli[i] && !s_cli[i].connected()) s_cli[i].stop();
      if (s_cli[i] && s_cli[i].connected()) {
        live++;
        while (s_cli[i].available()) s_cli[i].read();   // discard anything sent to us
      }
    }
    s_nclients = live;

    // --- drain --------------------------------------------------------------
    // Blocking here is FINE and is the whole point of the task: a stalled socket
    // stalls only this low-priority task, while ringPush() keeps returning instantly
    // for the UI and net tasks.
    if (live) {
      size_t n;
      while ((n = ringPop(buf, sizeof(buf))) > 0)
        for (int i = 0; i < kMaxClients; ++i)
          if (s_cli[i] && s_cli[i].connected()) s_cli[i].write(buf, n);
    } else {
      portENTER_CRITICAL(&s_mux);         // nobody reading — don't hoard stale bytes
      s_tail = s_head;
      portEXIT_CRITICAL(&s_mux);
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

}  // namespace

LogTee LOG;

size_t LogTee::write(uint8_t c) {
  Serial.write(c);
  if (s_nclients) ringPush(&c, 1);
  return 1;
}

size_t LogTee::write(const uint8_t *buf, size_t len) {
  Serial.write(buf, len);
  if (s_nclients) ringPush(buf, len);     // free when nobody is attached
  return len;
}

void logMirrorBegin(uint16_t port) {
  if (s_ring) return;
  s_port = port;
  s_ring = (uint8_t *)heap_caps_malloc(kRing, MALLOC_CAP_SPIRAM);
  if (!s_ring) s_ring = (uint8_t *)malloc(kRing);
  if (!s_ring) { Serial.println("[logmirror] ring alloc failed — disabled"); return; }
  // Core 0 with the rest of the network work, priority 1 so it sits below netTask (2)
  // and can never starve the Sonos poller or the ESP-Hosted link.
  xTaskCreatePinnedToCore(logTask, "logmirror", 4096, nullptr, 1, nullptr, 0);
}

int      logMirrorClients() { return s_nclients; }
uint32_t logMirrorDropped() { return s_dropped; }
uint16_t logMirrorPort()    { return s_port; }

#endif  // LOG_MIRROR
