// UI feedback tones on the NS4168 amp + onboard speakers. uiSoundPlay() itself is declared in
// core/board.h (it is part of the HAL contract); these two are board-internal lifecycle.
#pragma once

bool uiSoundInit();      // I2S + amp power pin. False if I2S refuses to start.
void uiSoundIdleTick();  // call periodically — powers the amp down once things go quiet
