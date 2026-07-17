// Board pin map — nulllab / emakefun ESP32-S3-CAM.
//
// Sources: the vendor's README pin table + pinout drawing + schematic
//   github.com/nulllaborg/esp32s3-cam
// See plans/04-sonos-button-plan.md §3/§4 for the pin-selection rationale.
//
// This unit ignores the camera entirely (the module unplugs from its FPC connector), so the
// only pins that matter are the button, the status LED, and knowing what NOT to touch.
#pragma once

// --- The button (the whole user interface) ---
// GPIO14: plain GPIO, NO strapping role, internal pull-up, unused by the camera or TF slot,
// and RTC-capable (leaves deep-sleep-wake open if this ever runs off the PH2.0 battery JST).
// Wire the momentary between this pin and GND — idle HIGH, pressed LOW. No external resistor.
#define PIN_BUTTON          14

// --- LEDs ---
// GPIO2: CONFIRMED from the vendor schematic (Extension Interface / LED block):
//   IO2 -> R6 1K -> D5 (yellow_green) -> GND, i.e. active-HIGH, ~1.2 mA.
// But IO2 is NOT broken out to J4/J5 — it only reaches the onboard D5, so it can only ever be
// seen with the case open. Useful as a liveness tell during bring-up; useless as product UI.
#define PIN_STATUS_LED       2

// GPIO47 (J4 pin 6): the FLM12-FJ-6's white ring, wired LOW-SIDE — the ring's + (white) goes to
// J4.1 (+5V) and its - (black) lands HERE, so the pin SINKS the ring current instead of sourcing
// it. That is the whole point: the ring is white (Vf ~3.1 V) and specced 5-24 V, so a 3.3 V pin
// cannot source enough to light it, but it can happily pull the cathode to GND.
//
//   LOW  -> ring sees the full 5 V -> ON   (pin sinks ~10-20 mA; the S3 is good for ~28 mA)
//   HIGH -> ring sees 5 - 3.3 = 1.7 V, well under Vf -> OFF, and no current flows into the pin
//
// SAFETY: drive this pin, always. As an INPUT (Hi-Z) the node floats up toward 5 V and is only
// stopped by the pin's ESD clamp at ~4 V — over the 3.6 V abs-max, albeit at leakage currents.
// So set OUTPUT+HIGH early and never leave it floating with the ring connected.
#define PIN_RING_GATE       47
// GPIO3: the two white "flashlight" LEDs. Documented by the vendor README, so it doubles as the
// control in this test: if GPIO3 lights and GPIO2 doesn't, the IO2 LED guess was wrong (rather
// than the build being broken). Bright — pulse it, don't leave it on.
#define PIN_FLASH_LED        3

// --- Committed by the board; do not reuse ---
//   Camera (DVP):  4 5 6 7 8 9 10 11 12 13 15 16 17 18
//   TF card (MMC): 38 (CMD) 39 (CLK) 40 (DAT0)
//   OPI PSRAM/flash: 26..37   <- never touch
//   USB:           19 (D-) 20 (D+)
//   UART0 console: 43 (TX) 44 (RX)
//   Strapping:     0 (BOOT) 45 46 (LOG)
//
// Free on the headers besides PIN_BUTTON: GPIO1 (RTC/ADC1, the best alternate), 47, 48,
// and 41/42 (MTDI/MTMS — usable, but taking them costs JTAG).
