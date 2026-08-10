#include "heap_watch.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

namespace heapwatch {
namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t s_low     = UINT32_MAX;
volatile uint32_t s_largest = 0;
volatile uint32_t s_atMs    = 0;
const char *volatile s_tag  = "";

}  // namespace

void note(const char *tag) {
  const uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

  // Fast path: the overwhelming majority of calls are not a new low, and must cost a load and a
  // compare. No lock, no largest-block walk.
  if (f >= s_low) return;

  // Largest-free-block walks the free list and takes the heap's own lock, so it CANNOT run inside
  // the critical section below.
  const uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

  portENTER_CRITICAL(&s_mux);
  if (f < s_low) {            // re-check: another task may have won the race
    s_low     = f;
    s_largest = largest;
    s_atMs    = millis();
    s_tag     = tag;
  }
  portEXIT_CRITICAL(&s_mux);
}

void worst(Low &out) {
  portENTER_CRITICAL(&s_mux);
  out.freeBytes   = (s_low == UINT32_MAX) ? 0 : s_low;
  out.largestFree = s_largest;
  out.atMs        = s_atMs;
  out.tag         = s_tag;
  portEXIT_CRITICAL(&s_mux);
}

void clear() {
  portENTER_CRITICAL(&s_mux);
  s_low = UINT32_MAX; s_largest = 0; s_atMs = 0; s_tag = "";
  portEXIT_CRITICAL(&s_mux);
}

}  // namespace heapwatch
