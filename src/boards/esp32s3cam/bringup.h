// Phase-0 bring-up self-test (ESP32-S3-CAM board): prove the board, the button pin choice and
// the status-LED guess over serial, BEFORE any app code exists. Built only by the
// `sleep-button-bringup` env (-DBUTTON_BRINGUP). Never returns — it is the whole program.
//
// Standalone on purpose: it links neither core/ nor a unit (there is no board.cpp or unit yet —
// those land in Phase 1), the same way mic_test.cpp / audio_test.cpp isolate one subsystem.
// See plans/04-sonos-button-plan.md §5 "Phases".
#pragma once

void camBringupRun();   // does not return — blinks the LEDs, prints live button transitions
