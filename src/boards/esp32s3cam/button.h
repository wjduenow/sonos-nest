// The one physical input on this board: a FILN FLM12-FJ-6 momentary to GND on PIN_BUTTON.
//
// Deliberately shaped like the crowpanel's knob rather than inventing a new HAL: core/board.h
// already describes "a press-classified momentary button" (KnobEvent Short/Long/Double/Triple),
// which is exactly what this is, so board.cpp maps knobEvent()/knobDown() straight onto these and
// the core needs no change. See plans/04-sonos-button-plan.md §5.
//
// This is the one board that classifies multi-presses, so it is the one where Short is delayed by
// the multi-press window (button.cpp, MULTI_GAP_MS). buttonDown() is NOT — press feedback belongs
// on that edge.
#pragma once

#include "core/board.h"   // KnobEvent

void      buttonInit();
KnobEvent buttonEvent();   // next queued event, or None. Sampled+debounced on each call.
bool      buttonDown();    // true while held (debounced)
