#include "encoder.h"
#include "../board_pins.h"
#include <ESP32Encoder.h>

// Most detent encoders emit 4 quadrature counts per physical click. We read the raw
// count and divide so encoderDelta() returns detents, not edges. If your knob feels
// 4x too sensitive (or 1 detent = 1 step), adjust COUNTS_PER_DETENT during bring-up.
static const int32_t COUNTS_PER_DETENT = 4;

static ESP32Encoder s_enc;
static int64_t      s_lastCount = 0;

void encoderInit() {
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  s_enc.attachFullQuad(PIN_ENCODER_A, PIN_ENCODER_B);
  s_enc.clearCount();
  s_lastCount = 0;
}

int32_t encoderDelta() {
  int64_t raw = s_enc.getCount();
  int32_t detents = (int32_t)((raw - s_lastCount) / COUNTS_PER_DETENT);
  if (detents != 0) {
    s_lastCount += (int64_t)detents * COUNTS_PER_DETENT;  // keep sub-detent remainder
  }
  return detents;
}
