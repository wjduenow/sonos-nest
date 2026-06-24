// Rotary encoder on direct GPIOs (A=42, B=4). Press is NOT here — see pcf8574.h.
#pragma once

#include <stdint.h>

void    encoderInit();
int32_t encoderDelta();   // signed steps since last call; clears accumulator
