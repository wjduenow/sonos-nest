// Serialises whole I2C transactions across tasks on this board's shared bus.
//
// WHY THIS EXISTS — Arduino's TwoWire is not safe to share the way you would assume. It does hold
// a per-bus mutex, but only inside each individual call. A GT911 register read is
//
//     beginTransmission -> write -> write -> endTransmission(false) -> requestFrom -> read...
//
// where `endTransmission(false)` issues a REPEATED START and deliberately leaves the bus held for
// the requestFrom that follows. Nothing in TwoWire stops another task's transaction landing in the
// middle of that span.
//
// This was not theoretical. The jukebox was single-user on the bus (GT911, read from the UI task)
// until the Modulino dial arrived on the knob task. The symptom was NOT a touch failure — it was
// the KNOB probe returning true for an address with nothing on it (a spurious "Modulino Knob found
// at 0x74" on a bus whose only devices are 0x5D and 0x2F), because a bare requestFrom that should
// have found no ACK picked up the byte count from the GT911's interleaved transfer. An uncontended
// scan in boardInit() reported the bus correctly, which is what makes this so easy to misread as a
// bad probe rather than a race.
//
// So: take this around a COMPLETE logical transaction — not around each Wire call — in every
// driver that touches this bus. The PCF8574 button expander will need it too.
#pragma once

#include <stdint.h>

void i2cBusInit();                          // call once, right after Wire.begin()
bool i2cBusLock(uint32_t timeoutMs = 100);  // false if the bus could not be claimed in time
void i2cBusUnlock();
