// Phase-0 bring-up self-test (Seeed XIAO ESP32S3): prove the build config, the two pin choices,
// the LED polarity and the ring's low-side drive over serial, BEFORE trusting the app on this
// board. Built only by the `button-v2-bringup` env (-DBUTTON_V2_BRINGUP). Never returns — it is
// the whole program.
//
// Standalone on purpose: it links neither core/ nor a unit, the same way the ESP32-S3-CAM's
// bringup.cpp and the es3c28p mic/audio tests isolate one subsystem.
// See plans/11-button-v2.md "Phase B".
#pragma once

void xiaoBringupRun();   // does not return — settles the LED polarity, then live button transitions
