// Rotary encoder on direct GPIOs (A=42, B=4). Press is NOT here — see pcf8574.h.
// Uses the ESP32 PCNT peripheral (via ESP32Encoder) so counting is hardware-driven and
// doesn't burn CPU or miss steps during LVGL redraws.
#pragma once

#include <stdint.h>

void    encoderInit();
int32_t encoderDelta();   // signed detents since last call; clears the accumulator
