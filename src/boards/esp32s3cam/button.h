// The one physical input on this board: a FILN FLM12-FJ-6 momentary to GND on PIN_BUTTON.
//
// Deliberately shaped like the crowpanel's knob rather than inventing a new HAL: core/board.h
// already describes "a press-classified momentary button" (KnobEvent Short/Long), which is
// exactly what this is, so board.cpp maps knobEvent()/knobDown() straight onto these and the
// core needs no change. See plans/04-sonos-button-plan.md §5.
#pragma once

#include "core/board.h"   // KnobEvent

void      buttonInit();
KnobEvent buttonEvent();   // next queued event, or None. Sampled+debounced on each call.
bool      buttonDown();    // true while held (debounced)
