// Phase-1 test: connect WiFi, SSDP-discover Sonos, then drive the first zone interactively
// (twist = volume, press = play/pause) against the real speaker. Plan §7 Phase 1 exit
// criterion: "the board can pause/resume real music on your Sonos."
//
// Enable with -DPHASE1_TEST (see platformio.ini env:phase1). main() runs this and never
// returns.
#pragma once

void phase1Run();
