// UI feedback tones on the NS4168 amp + onboard speakers. uiSoundPlay() itself is declared in
// core/board.h (it is part of the HAL contract); this is the board-internal lifecycle.
//
// There is deliberately no idle/tick entry point: the board's own sound task owns amp power-down.
// The unit used to call uiSoundIdleTick() from uiTick(), which meant units/sonos_jukebox reached
// straight into a board header — breaking the layering CLAUDE.md sets out, and hard-coupling the
// unit to this one board.
#pragma once

bool uiSoundInit();      // I2S + amp power pin + the sound task. False if any of it refuses.
