// XIAO ESP32S3 Phase-0 bring-up — the first code to run on the button-v2 board.
//
// The ESP32-S3-CAM's equivalent (boards/esp32s3cam/bringup.cpp) had to hunt for its ring pin
// across three candidates, because that board's four wires go onto an 8-pin header that is easy
// to miscount. This one does not: the wires are SOLDERED to four named pads, so if the ring does
// not light, the answer is the solder joint or the polarity, not the pin. The sweep is gone and
// three real unknowns take its place.
//
// It answers four questions, in the order that they'd block later phases:
//
//   1. Did the 8 MB-flash + OPI-PSRAM build config take? [env] targets a 16 MB board, so this env
//      must override flash_size and the partition table. PSRAM 0 or flash 16 MB means the env is
//      wrong and everything downstream is built on sand.
//   2. Which way round is the onboard LED? XIAO boards conventionally wire it active-LOW, the
//      opposite of the ESP32-S3-CAM's D5 — and it is the only light inside a closed case, so a
//      backwards guess reads as dead hardware. Both polarities are driven, labelled.
//   3. Is GPIO8 a good button pin? Prove idle=HIGH / pressed=LOW, one push = one event, and that
//      a press at power-on is harmless (it is not a strapping pin — which is what makes it safe
//      to overload as the "hold at boot = re-open the WiFi portal" trigger).
//   4. What is the press/release timing on YOUR switch? The Short/Long threshold here previews
//      the knobEvent() contract (core/board.h) that button_common/button.cpp implements, and the
//      held-time print is the tool to reach for if a double press ever registers as two singles.
//
// Built only by the `button-v2-bringup` env. See plans/11-button-v2.md.
#include "bringup.h"
#include "pins.h"
#include <Arduino.h>

// Debounce: a cheap tact/panel switch bounces for a few ms; 30 ms is the usual safe floor and
// is imperceptible to a human. Same value button_common/button.cpp ships — raise it only if the
// live log below shows chatter.
static const uint32_t DEBOUNCE_MS   = 30;
// Previews core/board.h's KnobEvent Short/Long split so the app inherits a threshold that was
// measured on the real button rather than guessed.
static const uint32_t LONG_PRESS_MS = 700;

static const uint32_t BLINK_MS      = 500;   // status LED: 1 Hz, visible but not frantic
static const uint32_t HEARTBEAT_MS  = 3000;

// Active-low: the internal pull-up holds the pin HIGH until the button shorts it to GND.
static inline bool buttonDown() { return digitalRead(PIN_BUTTON) == LOW; }

void xiaoBringupRun() {
  Serial.println("\n===== XIAO ESP32S3 (button-v2) Phase 0 bring-up =====");

  // --- 1. Memory / flash report -------------------------------------------------------
  // Expect: PSRAM 8388608, flash 8388608. PSRAM 0 => the qio_opi memory_type didn't take.
  // Flash 16 MB => the env inherited [env]'s flash_size and the partition table will be wrong.
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

  // The ring is LOW-SIDE (see pins.h): HIGH = off. Drive it before anything else — left as an
  // input it floats toward 5 V and only the ESD clamp stops it, which is out of spec.
  digitalWrite(PIN_RING_GATE, HIGH);   // set the level FIRST so enabling the driver can't glitch
  pinMode(PIN_RING_GATE, OUTPUT);
  digitalWrite(PIN_RING_GATE, HIGH);   // ring off

  // Boot-time button state. GPIO8 has no strapping role, so a press here must NOT affect boot —
  // this line is the evidence. It's also exactly the read the portal's hold-at-boot would use.
  Serial.printf("[bringup] button at boot: %s  (GPIO%d, idle should read UP)\n",
                buttonDown() ? "DOWN" : "up", PIN_BUTTON);

  // --- 2b. LED POLARITY TEST ----------------------------------------------------------
  // pins.h assumes active-LOW (PIN_STATUS_LED_ACTIVE_LOW). Rather than assert that, drive both
  // levels for long enough to see, and label them. Whichever one lights the LED is the answer;
  // set PIN_STATUS_LED_ACTIVE_LOW to match and never think about it again.
  Serial.println("\n[bringup] === LED POLARITY (GPIO21, onboard — look at the BOARD, not the ring) ===");
  Serial.printf("[bringup] pins.h currently assumes ACTIVE_%s.\n",
                PIN_STATUS_LED_ACTIVE_LOW ? "LOW" : "HIGH");
  for (int pass = 1; pass <= 3; ++pass) {
    Serial.printf("[led    ] pass %d: driving GPIO%d LOW  for 2 s -> lit if ACTIVE-LOW\n",
                  pass, PIN_STATUS_LED);
    digitalWrite(PIN_STATUS_LED, LOW);  delay(2000);
    Serial.printf("[led    ] pass %d: driving GPIO%d HIGH for 2 s -> lit if ACTIVE-HIGH\n",
                  pass, PIN_STATUS_LED);
    digitalWrite(PIN_STATUS_LED, HIGH); delay(2000);
  }
  Serial.println("[bringup] LED test done.\n");

  // --- 3. Ring check ------------------------------------------------------------------
  // No pin sweep here (see the header comment): four soldered pads, one candidate. A fade proves
  // the low-side drive AND that the 5V pad is really carrying VBUS.
  Serial.println("[bringup] === RING (GPIO9, low-side: LOW = lit) ===");
  Serial.println("[bringup] Expect three slow fades up and down. If it never lights, check in order:");
  Serial.println("[bringup]   white -> 5V pad, black -> D10 pad, and that you are on USB power.");
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_RING_GATE, 0);
  for (int pass = 0; pass < 3; ++pass) {
    for (int d = 255; d >= 0; d -= 5) { ledcWrite(0, d); delay(8); }   // duty down = brighter
    for (int d = 0; d <= 255; d += 5) { ledcWrite(0, d); delay(8); }
  }
  ledcDetachPin(PIN_RING_GATE);
  pinMode(PIN_RING_GATE, OUTPUT);
  digitalWrite(PIN_RING_GATE, HIGH);   // dark
  Serial.println("[bringup] Ring test done. It now follows the button.\n");

  // --- 4. Live loop -------------------------------------------------------------------
  Serial.printf("[bringup] blinking the onboard LED (GPIO%d) at 1 Hz, using the pins.h polarity.\n",
                PIN_STATUS_LED);
  Serial.printf("[bringup] >> Now PRESS the button on GPIO%d. Expect one PRESS + one RELEASE per\n",
                PIN_BUTTON);
  Serial.println("[bringup]    push, no repeats, and a Short/Long classification on release.");
  Serial.println("[bringup] >> Each press also TOGGLES the ring — a preview of the real product loop.");
  Serial.println("[bringup] >> For the multi-press window, watch the held= times: they are what");
  Serial.println("[bringup]    button_common/button.cpp's DEBOUNCE_MS / LONG_PRESS_MS are set from.");
  bool ringOn = false;

  bool     rawLast    = buttonDown();   // last raw sample
  bool     stable     = rawLast;        // debounced state
  uint32_t lastChange = millis();       // when the raw sample last flipped
  uint32_t pressedAt  = 0;
  uint32_t presses    = 0;
  uint32_t bounces    = 0;              // raw flips rejected by the debounce window
  bool     led        = false;
  uint32_t lastBlink  = 0;
  uint32_t lastBeat   = 0;

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
        // Toggle the ring on press — the same edge the real unit starts/stops Sonos on.
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

    // Status-LED blink, through the polarity pins.h currently declares.
    if (now - lastBlink >= BLINK_MS) {
      lastBlink = now;
      led = !led;
#if PIN_STATUS_LED_ACTIVE_LOW
      digitalWrite(PIN_STATUS_LED, led ? LOW : HIGH);
#else
      digitalWrite(PIN_STATUS_LED, led ? HIGH : LOW);
#endif
    }

    if (now - lastBeat >= HEARTBEAT_MS) {
      lastBeat = now;
      // `bounces` is diagnostic, not an error: a few per press is normal switch chatter that the
      // debounce absorbed. Hundreds while untouched means noise on the harness.
      Serial.printf("[alive  ] raw=%s presses=%lu bounces=%lu heap=%u\n",
                    raw ? "DOWN" : "up", (unsigned long)presses, (unsigned long)bounces,
                    (unsigned)ESP.getFreeHeap());
    }
    delay(2);
  }
}
