// The one physical input on a sonos-button: a FILN FLM12-FJ-6 momentary to GND on one pin.
//
// SHARED BY BOTH BUTTON BOARDS (esp32s3cam and xiao_esp32s3) — see this directory's README. The
// pin is passed to buttonInit() rather than taken from a pins.h, because that is the ONLY thing
// the two boards disagree about here. Everything below the pin — debounce window, long-press
// threshold, and the multi-press classifier — is one implementation on purpose: it is subtle
// enough that two divergent copies would be a bug waiting to happen.
//
// Deliberately shaped like the crowpanel's knob rather than inventing a new HAL: core/board.h
// already describes "a press-classified momentary button" (KnobEvent Short/Long/Double/Triple),
// which is exactly what this is, so board.cpp maps knobEvent()/knobDown() straight onto these and
// the core needs no change. See plans/04-sonos-button-plan.md §5.
//
// These are the boards that classify multi-presses, so this is where Short is delayed by the
// multi-press window (button.cpp, MULTI_GAP_MS). buttonDown() is NOT — press feedback belongs
// on that edge.
#pragma once

#include <stdint.h>
#include "core/board.h"   // KnobEvent

void      buttonInit(uint8_t pin);
KnobEvent buttonEvent();   // next queued event, or None. Sampled+debounced on each call.
bool      buttonDown();    // true while held (debounced)
