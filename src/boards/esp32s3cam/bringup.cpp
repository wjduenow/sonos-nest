// ESP32-S3-CAM Phase-0 bring-up — the first code to ever run on this board.
//
// It answers four questions, in the order that they'd block later phases:
//
//   1. Did the OPI-PSRAM + 8 MB-flash build config take? This board has 8 MB of flash, not the
//      16 MB the nest/sleep-machine units have, so it needs its own partition table. If PSRAM
//      reads 0 or flash reads 16 MB, the env is wrong and everything downstream is built on sand.
//   2. Is GPIO14 a good button pin? Prove idle=HIGH / pressed=LOW, that one push = one event,
//      and that a press at power-on is harmless (it's not a strapping pin — which is what makes
//      it safe to overload as the "hold at boot = re-open the WiFi portal" trigger in Phase 5).
//   3. Is there really an LED on GPIO2? The schematic's Extension Interface block shows one; the
//      vendor README never mentions it. It's the only status feedback a screenless box can give,
//      so it's worth knowing now. GPIO3 (the documented flashlight) is pulsed first as a CONTROL:
//      if GPIO3 lights and GPIO2 doesn't, the IO2 guess is wrong rather than the build broken.
//   4. What's the press/release timing actually like on your button? The Short/Long threshold
//      here previews the knobEvent() contract (core/board.h) that Phase 2 reuses.
//
// Built only by the `sleep-button-bringup` env. See plans/04-sonos-button-plan.md.
#include "bringup.h"
#include "pins.h"
#include <Arduino.h>

// Debounce: a cheap tact/panel switch bounces for a few ms; 30 ms is the usual safe floor and
// is imperceptible to a human. Raise it only if the live log below shows chatter.
static const uint32_t DEBOUNCE_MS  = 30;
// Previews core/board.h's KnobEvent Short/Long split so Phase 2 inherits a threshold that was
// measured on the real button rather than guessed.
static const uint32_t LONG_PRESS_MS = 700;

static const uint32_t BLINK_MS     = 500;   // status LED: 1 Hz, visible but not frantic
static const uint32_t HEARTBEAT_MS = 3000;

// Active-low: the internal pull-up holds the pin HIGH until the button shorts it to GND.
static inline bool buttonDown() { return digitalRead(PIN_BUTTON) == LOW; }

void camBringupRun() {
  Serial.println("\n===== ESP32-S3-CAM Phase 0 bring-up =====");

  // --- 1. Memory / flash report -------------------------------------------------------
  // Expect: PSRAM 8388608, flash 8388608. PSRAM 0 => the qio_opi memory_type didn't take.
  // Flash 16 MB => the env inherited the wrong flash_size and the partition table will be wrong.
  Serial.printf("[bringup] PSRAM: %u bytes total, %u free\n",
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  Serial.printf("[bringup] flash: %u bytes  (expect 8388608 = 8 MB on this board)\n",
                (unsigned)ESP.getFlashChipSize());
  Serial.printf("[bringup] internal heap free: %u bytes\n", (unsigned)ESP.getFreeHeap());
  if (ESP.getPsramSize() == 0)
    Serial.println("[bringup] WARNING: no PSRAM — the qio_opi build config did not take");
  if (ESP.getFlashChipSize() != 8 * 1024 * 1024)
    Serial.println("[bringup] WARNING: flash != 8 MB — wrong flash_size/partition table for this board");

  // --- 2. Pin setup -------------------------------------------------------------------
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_FLASH_LED,  OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  digitalWrite(PIN_FLASH_LED,  LOW);

  // The ring is LOW-SIDE (see pins.h): HIGH = off. Drive it before anything else — left as an
  // input it floats toward 5 V and only the ESD clamp stops it, which is out of spec.
  digitalWrite(PIN_RING_GATE, HIGH);   // set the level FIRST so enabling the driver can't glitch
  pinMode(PIN_RING_GATE, OUTPUT);
  digitalWrite(PIN_RING_GATE, HIGH);   // ring off

  // Boot-time button state. GPIO14 has no strapping role, so a press here must NOT affect boot —
  // this line is the evidence. It's also exactly the read Phase 5 would use for hold-at-boot.
  Serial.printf("[bringup] button at boot: %s  (GPIO%d, idle should read UP)\n",
                buttonDown() ? "DOWN" : "up", PIN_BUTTON);

  // --- 3. Flashlight control pulse ----------------------------------------------------
  // Documented by the vendor (README: "Flash light pin GPIO3"), so this is the known-good
  // reference for the GPIO2 guess below. Two short blips — these are BRIGHT, don't dwell.
  Serial.printf("[bringup] pulsing the flashlight LEDs (GPIO%d) twice — expect two bright white blips\n",
                PIN_FLASH_LED);
  for (int i = 0; i < 2; ++i) {
    digitalWrite(PIN_FLASH_LED, HIGH); delay(80);
    digitalWrite(PIN_FLASH_LED, LOW);  delay(200);
  }

  // --- 3b. Ring pin SWEEP -------------------------------------------------------------
  // The ring didn't cycle when we assumed black was on IO47, and the firmware was provably
  // running — so the wire, not the code, is the unknown. Rather than ask a human to re-count
  // header pins (the thing that just failed), drive each candidate LOW in turn and let the ring
  // identify its own pin. Whichever step lights it IS the pin black is in.
  //
  // Every candidate is driven HIGH when not selected — never left floating. With the ring's
  // cathode attached, a floating pin drifts toward 5 V and only the ESD clamp stops it.
  struct RingCand { int gpio; const char *where; };
  static const RingCand kCands[] = {
    {47, "J4.6  IO47  <- where we THINK black is"},
    { 1, "J4.3  IO1   <- if you counted from the wrong end, black is here"},
    {48, "J4.4  IO48"},
  };
  const int kNumCands = sizeof(kCands) / sizeof(kCands[0]);

  for (int i = 0; i < kNumCands; ++i) { digitalWrite(kCands[i].gpio, HIGH); pinMode(kCands[i].gpio, OUTPUT); digitalWrite(kCands[i].gpio, HIGH); }

  Serial.println("\n[bringup] === RING PIN SWEEP ===");
  Serial.println("[bringup] white stays on J4.1 (+5V). Watch the ring and note WHICH step lights it.");
  Serial.println("[bringup] If NO step lights it, black is on J4.5 (NC) or another dead pin.");
  Serial.println("[bringup] If it is lit the WHOLE time, black is still on GND (J4.2) — move it.");
  for (int pass = 1; pass <= 3; ++pass) {
    for (int i = 0; i < kNumCands; ++i) {
      Serial.printf("[sweep  ] pass %d: driving GPIO%-2d LOW  -> %s\n", pass, kCands[i].gpio, kCands[i].where);
      digitalWrite(kCands[i].gpio, LOW);
      delay(2500);
      digitalWrite(kCands[i].gpio, HIGH);
      delay(700);            // a deliberate dark gap so consecutive hits can't blur together
    }
    Serial.printf("[sweep  ] pass %d: all candidates HIGH -> ring should be DARK\n", pass);
    delay(2000);
  }
  Serial.println("[bringup] Sweep done. The ring now follows the button (IO47 assumed).\n");

  // --- 4. Live loop -------------------------------------------------------------------
  Serial.printf("[bringup] blinking the onboard status LED (GPIO%d) at 1 Hz.\n", PIN_STATUS_LED);
  Serial.println("[bringup] >> Now PRESS the button on GPIO14. Expect one PRESS + one RELEASE per push,");
  Serial.println("[bringup]    no repeats, and a Short/Long classification on release.");
  Serial.println("[bringup] >> Each press also TOGGLES the ring — a preview of the real product loop.");
  bool ringOn = false;

  bool     rawLast      = buttonDown();   // last raw sample
  bool     stable       = rawLast;        // debounced state
  uint32_t lastChange   = millis();       // when the raw sample last flipped
  uint32_t pressedAt    = 0;
  uint32_t presses      = 0;
  uint32_t bounces      = 0;              // raw flips rejected by the debounce window
  bool     led          = false;
  uint32_t lastBlink    = 0;
  uint32_t lastBeat     = 0;

  for (;;) {
    const uint32_t now = millis();

    // Debounce: only accept a level that has held steady for DEBOUNCE_MS.
    const bool raw = buttonDown();
    if (raw != rawLast) {
      rawLast = raw;
      lastChange = now;
      if (raw != stable) bounces++;   // a flip that may or may not survive the window
    }
    if (raw != stable && (now - lastChange) >= DEBOUNCE_MS) {
      stable = raw;
      if (stable) {
        pressedAt = now;
        presses++;
        // Toggle the ring on press — the same edge the real unit will start/stop Sonos on.
        ringOn = !ringOn;
        digitalWrite(PIN_RING_GATE, ringOn ? LOW : HIGH);   // low-side: LOW = on
        Serial.printf("[button ] PRESS   #%lu  -> ring %s\n", (unsigned long)presses,
                      ringOn ? "ON" : "OFF");
      } else {
        const uint32_t held = now - pressedAt;
        Serial.printf("[button ] RELEASE held=%lu ms -> %s\n", (unsigned long)held,
                      held >= LONG_PRESS_MS ? "Long" : "Short");
      }
    }

    // Status-LED blink (the GPIO2 hypothesis under test).
    if (now - lastBlink >= BLINK_MS) {
      lastBlink = now;
      led = !led;
      digitalWrite(PIN_STATUS_LED, led ? HIGH : LOW);
    }

    if (now - lastBeat >= HEARTBEAT_MS) {
      lastBeat = now;
      // `bounces` is diagnostic, not an error: a few per press is normal switch chatter that the
      // debounce absorbed. Hundreds while untouched means noise — see the 100 nF note in the plan.
      Serial.printf("[alive  ] raw=%s presses=%lu bounces=%lu heap=%u\n",
                    raw ? "DOWN" : "up", (unsigned long)presses, (unsigned long)bounces,
                    (unsigned)ESP.getFreeHeap());
    }
    delay(2);
  }
}
