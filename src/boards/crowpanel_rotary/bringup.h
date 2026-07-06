// Phase-0 bring-up self-test. Verifies each subsystem over serial so you can confirm
// the board before building anything on top. See plan §7 Phase 0.
//
// Enable by building with -DPHASE0_BRINGUP (see platformio.ini). main() then runs this
// instead of the normal app and never returns from it.
#pragma once

void bringupRun();   // does not return — loops printing encoder/button live state
