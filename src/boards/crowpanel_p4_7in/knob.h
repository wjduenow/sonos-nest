// Arduino Modulino® Knob (SKU ABX00107) on the shared I2C bus — the jukebox's rotary dial.
//
// Implements the encoder/knob half of core/board.h. Unlike the nest, whose EC11 is wired straight
// to GPIOs and decoded by hardware PCNT, this dial is a *bus device*: an STM32C011 does the
// quadrature decoding and reports an accumulated position. So there is no PCNT unit, no
// counts-per-edge decoding and no ESP32Encoder here — just a 4-byte read.
//
// Wiring: J13 (Crowtail I2C, 4-pin 2.0 mm) -> Adafruit 4528 adapter -> the Knob's Qwiic socket.
// See hardware/jukebox-7/README.md; the two connectors are NOT the same and the pin orders are
// mirrored.
#pragma once

// Probes the dial and starts the poll task. Returns true if the dial answered *now* — but a false
// return is not fatal and not final: the task keeps re-probing, so plugging the dial in later
// works without a reflash or a reboot.
bool knobInit();
