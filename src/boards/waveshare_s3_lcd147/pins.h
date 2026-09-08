// Board pin map — Waveshare ESP32-S3-LCD-1.47B (the `button-v3` unit).
//
// Sources, in order of trust:
//   1. The board's OWN factory firmware — the ESP-IDF app descriptor at flash 0x10000 reads
//      `ESP32-S3-LCD-1.47B`, which is how this board was identified in the first place.
//   2. Waveshare's vendor demo driver headers (ESP32-S3-LCD-1.47B-Demo.zip,
//      Arduino/examples/LVGL_Arduino/{Display_ST7789,I2C_Driver,RGB_lamp,SD_Card,BAT_Driver}.h).
//      These are the better witness than the wiki tables: they are what the shipped demo compiles.
//   3. The wiki's own interface tables (they agree with 2 on every pin they list).
// See plans/14-button-v3.md.
//
// Same product as sonos-button and button-v2 — press to start the configured playlist — plus a
// screen that is DARK by default and wakes to show a QR code. Same ESP32-S3R8 as the nest and the
// XIAO, so it stays on the pinned platform (espressif32@6.9.0 / Arduino 2.0.17) and needs no
// tools/pio change. 16 MB flash here, though, not the 8 MB both other buttons have.
#pragma once

// ---------------------------------------------------------------------------------------------
// ONBOARD — committed by the board itself. Do not reuse any of these.
// ---------------------------------------------------------------------------------------------

// --- LCD: ST7789, 172x320, 4-wire SPI ---
#define PIN_LCD_MOSI         45
#define PIN_LCD_SCLK         40
#define PIN_LCD_CS           42
#define PIN_LCD_DC           41
#define PIN_LCD_RST          39
#define PIN_LCD_BL           46      // LEDC PWM, active-high

#define LCD_WIDTH           172      // native, portrait
#define LCD_HEIGHT          320

// ✅ VERIFIED ON HARDWARE 2026-09-08 (bring-up run 1): 320x172 landscape draws correctly with
// this offset — six equal-width colour bars reaching both ends of the long axis with no wrapped
// strip, which a wrong offset cannot produce.
//
// ⚠️ THE PANEL IS OFFSET INSIDE THE CONTROLLER'S RAM, AND GETTING THIS WRONG IS NOT A CRASH.
// The ST7789 has 240 columns of GRAM; this glass only has 172 of them, wired to columns 34..205.
// Every address window therefore needs +34 on X. Omit it and the picture still draws — shifted,
// with a 34 px band of garbage at one edge — which reads as "bad panel" rather than "bad constant".
// Arduino_GFX 1.3.1 takes it as a constructor argument (col_offset1), so there is nothing to patch.
#define LCD_COL_OFFSET       34
#define LCD_ROW_OFFSET        0

// Rotation 1 = landscape, 320 x 172 logical.
//
// This is a CASE decision, not an electrical one. The screen mounts on a SIDE WALL of the box with
// the button on top, and a button-sized box is wider than it is tall — the panel's active area is
// ~32.4 x 17.4 mm, so its long axis has to lie along the box's long axis or it does not fit the
// wall at all. Portrait (0) is one edit here plus a re-derive of the case cutout.
#define DISPLAY_ROTATION      1
// The panel wants colour inversion on (the ST7789 "ips" flag), same as the es3c28p's ILI9341.
// ✅ VERIFIED ON HARDWARE 2026-09-08: the bring-up's bars render red/green/blue/cyan/magenta/
// yellow in that order, so there is no R-B swap and this flag is correct. It was a guess carried
// over from the other SPI panel; it happened to be right.
#define LCD_INVERT_COLORS  true

// --- Shared I2C: QMI8658 6-axis IMU ---
#define PIN_I2C_SDA          48
#define PIN_I2C_SCL          47
#define I2C_FREQ_HZ      400000

// ✅ VERIFIED ON HARDWARE 2026-09-08: this board answers at 0x6B and returns WHO_AM_I = 0x05.
//
// ⚠️ 7-BIT. The QMI8658 datasheet quotes 0xD6/0xD4, which are 8-BIT addresses; Arduino's Wire is
// 7-bit, so they are 0x6B/0x6A here. The vendor demo agrees (Gyro_QMI8658.h). This is the same
// off-by-a-shift that cost real time on the jukebox's Modulino dial — see CLAUDE.md.
// Which of the two applies depends on the SA0 strap; probe both, take whichever ACKs.
#define QMI8658_ADDR_LOW   0x6B      // SA0 low  — what this board is expected to answer at
#define QMI8658_ADDR_HIGH  0x6A      // SA0 high — probed as a fallback

// --- WS2812 RGB bead ---
// Inside the case once assembled, so it is a bring-up liveness tell, never product UI — exactly
// the role the XIAO's GPIO21 LED plays. The button's own ring is the indicator that ships.
#define PIN_RGB_LED          38

// --- microSD (SDMMC, 4-bit) ---
// Not mounted by the app: this unit has nothing to store. Recorded so nobody reuses the pins, and
// because a future cache would want them. localStorageRoot() returns nullptr.
#define PIN_SD_CLK           14
#define PIN_SD_CMD           15
#define PIN_SD_D0            16
#define PIN_SD_D1            18
#define PIN_SD_D2            17
#define PIN_SD_D3            21

// --- Battery sense ---
// Divider onto ADC. The vendor demo reads it with analogReadMilliVolts() and scales by 3.0 with a
// 0.992857 correction factor. Unused by this unit (permanently USB-powered), recorded for reuse.
#define PIN_BAT_ADC           1

// --- Onboard BOOT button ---
// Strapping pin. NOT the product button — it is on the PCB, unreachable once the case is closed.
#define PIN_BOOT_BUTTON       0

// ---------------------------------------------------------------------------------------------
// THE HARNESS — the two wires that make this a button.
//
// The pin header is VERIFIED from Waveshare's interface drawing
// (wiki image ESP32-S3-LCD-1.47B-details-inter.jpg), which labels every pad:
//
//     LEFT  rail, USB-C end first:  5V(VBUS)  GND  3V3(OUT)  GP0  GP2  GP3  GP4  GP5  GP6
//     RIGHT rail, USB-C end first:  TX  RX  VBAT  GND  GP11  GP10  GP9  GP8  GP7
//
// Everything lands on the LEFT rail, so the harness has one exit and the right rail stays free:
//
//     5V   (pad 1)  -> white  (ring +)
//     GND  (pad 2)  -> brown  (switch)
//     GP2  (pad 5)  -> brown  (switch)
//     GP4  (pad 7)  -> black  (ring -, switched low-side by the pin itself — no MOSFET)
//
// GP3 sits between them and is deliberately skipped: it is the S3's JTAG-select strapping pin,
// the one strapping pin on this header, exactly as xiao_esp32s3/pins.h skips D2 for the same
// reason. The gap in the wiring is the reminder.
//
// GP2 and GP4 are both RTC- and ADC-capable, so deep-sleep wake stays available if anyone ever
// runs one off the VBAT pad.
// ✅ GP2 VERIFIED ON HARDWARE 2026-09-08: idle reads HIGH with the internal pull-up and nothing
// wired, so it is a sound button pin. GP4/the ring is still unproven — nothing is soldered yet.
#define PIN_BUTTON            2
#define PIN_RING_GATE         4

// The ring is white (Vf ~3.1 V) and specced 5-24 V, so a 3.3 V pin cannot SOURCE it — both other
// buttons wire it low-side off a 5 V rail and let the pin SINK the cathode. **This board brings
// VBUS out on header pad 1** (verified from the drawing above), so that arrangement carries over
// from button-v2 unchanged. It was the one open question that could have forced a BOM change.
//
//   LOW  -> ring sees the full 5 V -> ON   (pin sinks ~10-20 mA; the S3 is good for ~28 mA)
//   HIGH -> ring sees 5 - 3.3 = 1.7 V, well under Vf -> OFF, and no current flows into the pin
//
// SAFETY: drive this pin, always. As an INPUT (Hi-Z) the node floats up toward 5 V and is stopped
// only by the pin's ESD clamp at ~4 V — over the 3.6 V abs-max, albeit at leakage currents.
// board.cpp sets OUTPUT+HIGH as the first thing in boardInit(); the unavoidable exposure is
// reset -> boardInit(), the same window both other button units have lived with.
//
// NOTE the 5V pad is VBUS, present only when USB is. This unit is permanently USB-powered, so
// that is always. Run one off VBAT instead and the ring simply goes dark with the pin safe (no
// 5 V node for it to float toward) — the right failure, so it needs no guard.

// ---------------------------------------------------------------------------------------------
// Committed by the silicon; never touch.
//   OPI PSRAM / flash: 26..37
//   USB:               19 (D-) 20 (D+)      — the S3's native USB-Serial-JTAG, this board's console
//   UART0:             43 (TX) 44 (RX)
//   Strapping:         0 (BOOT), 3 (JTAG select), 45, 46
//
// Free and unclaimed on the header after the harness above: GP5, GP6 (left rail) and
// GP7..GP11 (right rail), plus UART0 TX/RX and the VBAT pad.
// ---------------------------------------------------------------------------------------------

// --- Antenna ---
// Unlike the XIAO, this board has an ONBOARD ceramic antenna (wiki §Onboard Resources item 7).
// No u.FL connector, no pigtail, no antenna pocket in the case — a real simplification over
// hardware/button-v2/, whose lid is structural partly because of the u.FL routing.

// --- Mechanical (for hardware/button-v3/) -------------------------------------------------
// From Waveshare's dimension drawing, ESP32-S3-LCD-1.47B-details-size.jpg:
//   PCB outline      36.37 x 20.32 mm
//   Mounting holes   4, one per corner, brass eyelets, labelled **M2**
//   Header pitch     2.54 mm, 9 pads per rail
// ⚠️ HOLE CENTRES ARE NOT RECORDED HERE ON PURPOSE. The drawing carries 1.97 / 2.40 / 2.00 /
// 3.52 / 17.78 without unambiguous leaders, and 17.78 is exactly 7 x 2.54 — far more likely to be
// the rail-to-rail spacing than a hole pitch. Measure them before deriving a mounting plate; this
// project has already paid once for trusting a datasheet number over calipers (the FLM12-FJ-6
// depth, CLAUDE.md).
