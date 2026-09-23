// QMI8658 6-axis IMU on the shared I2C bus — used here for ONE thing: knock-to-wake.
//
// The board has no touch panel, so tapping the case is the only gesture that reaches a sealed box.
// See core/board.h's tapDetected() for the contract this backs.
#pragma once

#include <stdint.h>

// Probe both 7-bit addresses (SA0 strap), verify WHO_AM_I, and configure the accelerometer.
// Returns false if nothing ACKs — in which case tap wake is simply unavailable and the button
// press still wakes the screen, so this is never fatal. Wire.begin() must already have run.
bool imuInit();

// True once per detected tap. The sampling happens INSIDE this call (self-rate-limited to one
// I2C read every POLL_MS), so there is nothing to latch and nothing to miss — but for the same
// reason it must be called steadily, every uiTick, not only when something is listening.
bool imuTapDetected();

// The 7-bit address that answered, or 0 if imuInit() found nothing. For bring-up logging.
uint8_t imuAddress();
