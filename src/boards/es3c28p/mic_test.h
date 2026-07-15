// Microphone capture bring-up (ES3C28P board): pull PCM off the onboard MEMS mic through the
// ES8311 codec's ADC/record path and print a live level meter + SRAM headroom. Built only by
// the `sleep-machine-mic` env. Never returns (call it as the whole program, like audioTestRun()).
//
// This is Phase 0 of a possible wake-word / voice-control feature: it proves the record path
// works in isolation (mirror of audio_test.cpp, which proved playback) and measures the free
// internal SRAM — the resource a wake-word engine is tightest against. See the feasibility
// notes in plans/ before building on this.
#pragma once

void micTestRun();
