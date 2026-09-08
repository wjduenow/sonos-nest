#include "inbound_gate.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "core/net/logmirror.h"   // LOG — the wait/timeout lines are how the gate is measured

namespace inbound {

// A mutex rather than a binary semaphore for the priority inheritance: the art tile worker sits at
// priority 1 and netTask-adjacent callers higher, and a low-priority holder being pre-empted while
// it holds the only slot is exactly the stall this must not add.
// Created at static-initialisation time, NOT lazily on first acquire(): the first callers are three
// tasks (artTask, the Spotify worker, the tile worker) that can all reach the gate in the same
// millisecond at boot, and a lazy `if (!s_mx) s_mx = create()` lets two of them create two mutexes —
// one of which release() then gives back to the wrong owner, and the gate is broken for good. Static
// init on ESP32 Arduino runs inside app_main's task with the scheduler up, so FreeRTOS calls are legal
// here; smapi::Client's static instances create their mutexes the same way.
static SemaphoreHandle_t    s_mx        = xSemaphoreCreateMutex();
static const char *volatile s_holder    = "";
static volatile uint32_t    s_heldSince = 0;
static Stats                s_st        = {0, 0, 0, 0, 0, "", "", 0};

bool acquire(const char *tag, uint32_t maxWaitMs) {
  if (!s_mx) return false;
  const uint32_t t0 = millis();
  // Try without waiting first so an uncontended take costs nothing and is not counted as a wait.
  bool got = xSemaphoreTake(s_mx, 0) == pdTRUE;
  if (!got) {
    const char *who = s_holder;
    ++s_st.waits;
    got = xSemaphoreTake(s_mx, pdMS_TO_TICKS(maxWaitMs)) == pdTRUE;
    const uint32_t waited = millis() - t0;
    if (waited > s_st.maxWaitMs) s_st.maxWaitMs = waited;
    if (!got) {
      ++s_st.timeouts;
      LOG.printf("[gate] %s gave up after %lu ms — %s has held it %lu ms; proceeding ungated\n", tag,
                 (unsigned long)waited, s_holder, (unsigned long)(millis() - s_heldSince));
      return false;
    }
    // Waits are the whole point: each one is a burst that did NOT happen. Logged over 250 ms so the
    // serial log shows the serialisation without printing every tile.
    if (waited >= 250)
      LOG.printf("[gate] %s waited %lu ms for %s\n", tag, (unsigned long)waited, who);
  }
  ++s_st.acquires;
  s_holder    = tag;
  s_heldSince = millis();
  return true;
}

void release() {
  if (!s_mx) return;
  const uint32_t held = millis() - s_heldSince;
  if (held > s_st.maxHeldMs) { s_st.maxHeldMs = held; s_st.maxHeldBy = s_holder; }
  s_holder = "";
  xSemaphoreGive(s_mx);
}

void stats(Stats &out) {
  out         = s_st;
  out.holder  = s_holder;
  out.heldMs  = out.holder[0] ? (millis() - s_heldSince) : 0;
}

}  // namespace inbound
