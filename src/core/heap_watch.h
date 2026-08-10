// Attribute the internal-heap low-water mark to whatever was running when it happened.
//
// WHY THIS EXISTS. `heapMin` in the health JSON says how close the device came to dying — below
// ~15 KB free, LWIP cannot get socket buffers and the board answers ping and nothing else — but it
// does not say WHAT took it there, and the dips are transient and rare. A live reading of 8.2 KB
// min was observed with no way to tell whether it came from a GENA event body, an album-art fetch,
// a favourites refresh or the Amazon crawl. Guessing between those cost real time and one boot
// loop, so: measure instead.
//
// HOW. Sprinkle note("tag") immediately AFTER the allocations you suspect, while they are still
// held — that is where the trough is, not at entry or after the free. Whenever a call sees a new
// global low it records the value, the largest free block, the uptime and the tag. The health JSON
// then reports which tag owns the worst moment since boot.
//
// COST. The fast path is one heap_caps_get_free_size() and a compare — no lock, no allocation.
// Only a genuine new low takes the (short) critical section and computes the largest free block,
// which walks the free list and is the expensive part. Calling this on a hot path is fine.
//
// TAGS MUST BE STRING LITERALS. Only the pointer is stored, never a copy — passing a String's
// c_str() or a stack buffer leaves a dangling pointer for the health handler to read.
#pragma once

#include <Arduino.h>

namespace heapwatch {

// Sample internal free heap; remember this point if it is the deepest seen so far.
void note(const char *tag);

struct Low {
  uint32_t freeBytes   = 0;          // internal free heap at the low point
  uint32_t largestFree = 0;          // largest contiguous block there — the number LWIP cares about
  uint32_t atMs        = 0;          // millis() when it happened
  const char *tag      = "";         // "" until the first note() call
};

// Deepest point seen since boot (or since clear()).
void worst(Low &out);

// Forget the low-water mark. For "watch what THIS does" measurements — otherwise one early boot
// dip masks everything that follows.
void clear();

}  // namespace heapwatch
