// Wake-word bring-up (ES3C28P board) — Phase 1 for on-device wake-word detection.
// Step 1: prove TensorFlow Lite Micro compiles on this toolchain, that the microWakeWord
// streaming op set is satisfiable, that a v2 model loads, and that its tensor arena fits
// (allocated in PSRAM). Feature frontend + real inference come in later steps. Built only by
// the `sleep-machine-wake` env. Never returns (call it as the whole program, like micTestRun()).
#pragma once

void wakeTestRun();
