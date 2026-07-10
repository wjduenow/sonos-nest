// Shared microSD mount for the ES3C28P board (SDIO 4-bit, pins in pins.h). Both the local
// audio player and the HTTP media server read from the card, so they mount it through here.
#pragma once

bool sdEnsureMounted();   // mount the card once; true if it's mounted and usable
