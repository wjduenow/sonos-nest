// Board pin map — Seeed Studio XIAO ESP32S3 (the `button-v2` unit).
//
// Sources: the Seeed wiki pinout + the Arduino core's own variant header, which is the better
// witness because it is what the build actually compiles against:
//   ~/.platformio/packages/framework-arduinoespressif32/variants/XIAO_ESP32S3/pins_arduino.h
// See plans/11-button-v2.md for why this board replaced the ESP32-S3-CAM.
//
// Same product as the original sonos-button, same FILN FLM12-FJ-6, same four wires — on a board
// a quarter the size. ESP32-S3R8, so it is the SAME SILICON the nest runs: 8 MB OPI PSRAM,
// 8 MB flash, dual-core Xtensa.
#pragma once

// ---------------------------------------------------------------------------------------------
// The wiring. All four wires land on the RIGHT-HAND pad rail, within ~7.6 mm of each other, so
// the harness has one exit and the whole left rail plus the far edge stays clear for the u.FL
// antenna. Rail order from the USB-C end is: 5V, GND, 3V3, D10, D9, D8, D7.
//
//     5V   (rail pad 1)   -> white  (ring +)
//     GND  (rail pad 2)   -> brown  (switch)
//     D10  (rail pad 4)   -> black  (ring -, switched low-side by the pin itself — no MOSFET)
//     D9   (rail pad 5)   -> brown  (switch)
//
// The XIAO has NO mounting holes and NO header sockets: these are soldered directly to the
// castellated pads. See hardware/button-v2/ for how the case takes the USB-C cable load.
// ---------------------------------------------------------------------------------------------

// --- The button (the whole user interface) ---
// D9 = GPIO8. No strapping role on the S3, and GPIO1..9 are all RTC-capable, so deep-sleep wake
// stays available if this ever runs off the battery pads. Wire the momentary between this pin and
// GND — idle HIGH, pressed LOW, internal pull-up, no external resistor.
#define PIN_BUTTON           8

// --- The ring ---
// D10 = GPIO9, wired LOW-SIDE exactly as on the ESP32-S3-CAM: the ring's + (white) goes to the
// 5V pad and its - (black) lands HERE, so the pin SINKS the ring current instead of sourcing it.
// That is the whole point — the ring is white (Vf ~3.1 V) and specced 5-24 V, so a 3.3 V pin
// cannot source enough to light it, but it can happily pull the cathode to GND.
//
//   LOW  -> ring sees the full 5 V -> ON   (pin sinks ~10-20 mA; the S3 is good for ~28 mA)
//   HIGH -> ring sees 5 - 3.3 = 1.7 V, well under Vf -> OFF, and no current flows into the pin
//
// SAFETY: drive this pin, always. As an INPUT (Hi-Z) the node floats up toward 5 V and is only
// stopped by the pin's ESD clamp at ~4 V — over the 3.6 V abs-max, albeit at leakage currents.
// So set OUTPUT+HIGH early and never leave it floating with the ring connected. board.cpp does
// this as the first thing in boardInit(); the unavoidable exposure is reset -> boardInit(), which
// is the same window the ESP32-S3-CAM unit has lived with.
//
// NOTE the 5V pad is VBUS. This unit is permanently USB-powered, so it is always present — but
// if anyone ever runs one off the battery pads, the ring goes dark and the pin is safe (no 5 V
// node to float toward). That is the right failure, so it needs no guard.
#define PIN_RING_GATE        9

// --- LEDs ---
// GPIO21 is the XIAO's onboard user LED (the variant's LED_BUILTIN). It is NOT on a pad — it can
// only be seen with the case open, so it is a liveness tell during bring-up, never product UI.
// The button's own ring is the real indicator, exactly as on the ESP32-S3-CAM (whose GPIO2 LED
// has the same limitation).
#define PIN_STATUS_LED      21
// ⚠️ UNVERIFIED POLARITY. XIAO boards conventionally wire the user LED active-LOW (3V3 -> R -> LED
// -> GPIO), the opposite of the ESP32-S3-CAM's active-HIGH D5. `button-v2-bringup` drives it both
// ways and prints which one lit, because getting this backwards means a "dead" LED that is
// actually just inverted — and this is the only status light inside the case.
#define PIN_STATUS_LED_ACTIVE_LOW  1

// --- Committed by the board; do not reuse ---
//   OPI PSRAM / flash: 26..37   <- never touch
//   USB:               19 (D-) 20 (D+)     — not brought out to pads
//   UART0 console:     43 (TX) 44 (RX)     = D6 / D7. Free-ish (the XIAO's console is USB-CDC),
//                                            but taking them costs the serial fallback. Don't.
//   Strapping:         0 (BOOT, the tact) 45 46, and **3 (JTAG source select) = D2** — the one
//                      strapping pin that IS on a pad. Avoid D2.
//   Bottom B2B:        the camera/mic pads of the XIAO Sense. Unpopulated on the plain board.
//
// Free on the pads besides PIN_BUTTON and PIN_RING_GATE: D0 (GPIO1), D1 (GPIO2), D3 (GPIO4),
// D4 (GPIO5, SDA), D5 (GPIO6, SCL), D8 (GPIO7). All ADC- and RTC-capable.

// --- Antenna ---
// ⚠️ THIS BOARD HAS NO ONBOARD ANTENNA — only a u.FL/IPEX connector and the detachable antenna
// that ships in the box. It is not optional: without it Wi-Fi range collapses and the PA drives
// an open connector. The case models a pocket for it at the end wall opposite the USB-C, as far
// from the button's Ø14 metal body as the box allows (~8 mm, vs ~4 mm on the cam-button, which
// works). No GPIO is involved — unlike the XIAO ESP32C6, there is no RF switch to enable.
