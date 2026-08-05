// Bounded, recycling tile-artwork cache for the Radio carousel.
//
// WHY BOUNDED IS NOT OPTIONAL. A 72x72 tile decoded to RGB565 is 10,368 bytes. Fifty station rows
// would be ~506 KB — as much as the entire LVGL pool — so an unbounded cache does not merely waste
// memory, it makes the carousel design unbuildable. This keeps a fixed ring of decoded slots in
// PSRAM and recycles the least-recently-used one, so cost is O(slots), not O(stations).
//
// The decoded buffers live in PSRAM and are handed to LVGL as lv_image_dsc_t pointing at our own
// memory, so they never enter the LVGL pool at all. The pool only ever holds the widgets.
//
// Two layers, because the expensive part is the network, not the decode:
//   * disk  — the resized JPEG under <storage>/radio/art/<STATION_KEY>.jpg, ~4.6 KB. Survives
//             reboots, so a genre browsed once never refetches.
//   * RAM   — `slots` decoded tiles, LRU.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace artcache {

// tilePx is the on-screen tile edge; slots is how many decoded tiles stay resident.
// 12 slots at 72 px is ~124 KB of PSRAM — comfortably more than a viewport, cheap on this board.
bool init(int tilePx = 72, int slots = 12);

// A ready tile, or nullptr. On a miss the fetch is queued (deduplicated) and nullptr returned; the
// caller draws its placeholder and repaints when generation() changes. Never blocks.
const lv_image_dsc_t *get(const String &stationKey, const String &artUrl);

// Bumps whenever a tile finishes decoding. The UI compares it to decide when to re-scan its rows,
// rather than polling every tile every frame.
uint32_t generation();

// STATION_KEY out of a SMAPI id ("catalog/stations/A3SP31LN235GV3/#chunk-..." -> "A3SP31LN235GV3").
// Keyed on the station rather than the id because the id's #chunk- changes on every browse while
// the artwork does not — 50/50 identical art URLs vs 0/50 identical chunks, measured.
String keyOf(const String &stationId);

}  // namespace artcache
