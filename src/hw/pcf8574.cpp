#include "pcf8574.h"
#include "../board_pins.h"
#include <Arduino.h>
#include <Wire.h>

// Input pins are held high in the shadow (quasi-bidirectional pull-up). Everything else
// starts high too; pcfLcdPower/pcfLcdReset/pcfTouchReset drive the outputs as needed.
static uint8_t s_shadow = 0xFF;
static const uint8_t kInputMask = (1 << PCF_PIN_KNOB_BTN) | (1 << PCF_PIN_TOUCH_INT);

// Button debounce + press-classification state (active-low: pressed pulls the line to 0).
static bool     s_btnStable   = false;  // true = pressed
static bool     s_btnLastRead = false;
static uint32_t s_btnChangeMs = 0;
static uint32_t s_pressStart  = 0;      // when the current press began
static bool     s_longFired   = false;  // Long already emitted for this press
static KnobEvent s_event      = KnobEvent::None;
static const uint32_t kDebounceMs = 25;
static const uint32_t kLongMs    = 600;

bool pcf8574Init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  Wire.beginTransmission(PCF8574_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;  // no ACK — wrong address or wiring
  }
  // Inputs high, outputs default high (LCD/touch held in a safe state until init drives them).
  s_shadow = 0xFF;
  pcf8574Write(s_shadow);
  return true;
}

uint8_t pcf8574Read() {
  // Ensure input pins are released high before reading.
  if ((s_shadow & kInputMask) != kInputMask) {
    s_shadow |= kInputMask;
    pcf8574Write(s_shadow);
  }
  Wire.requestFrom((int)PCF8574_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void pcf8574Write(uint8_t mask) {
  s_shadow = mask | kInputMask;  // never drive input pins low
  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(s_shadow);
  Wire.endTransmission();
}

void pcfSetPin(uint8_t pin, bool high) {
  if (high) s_shadow |= (1 << pin);
  else      s_shadow &= ~(1 << pin);
  pcf8574Write(s_shadow);
}

bool pcfGetPin(uint8_t pin) {
  return (pcf8574Read() >> pin) & 0x1;
}

void pcfLcdPower(bool on) { pcfSetPin(PCF_PIN_LCD_PWR, on); }

void pcfLcdReset() {
  // Mirrors Elecrow's proven RotaryScreen_2_1.ino sequence: high, low, high.
  pcfSetPin(PCF_PIN_LCD_RST, true);
  delay(100);
  pcfSetPin(PCF_PIN_LCD_RST, false);
  delay(120);
  pcfSetPin(PCF_PIN_LCD_RST, true);
  delay(120);  // ST7701 needs time after reset before commands
}

void pcfTouchReset() {
  pcfSetPin(PCF_PIN_TOUCH_RST, false);
  delay(10);
  pcfSetPin(PCF_PIN_TOUCH_RST, true);
  delay(50);
}

// Call this frequently (UI tick). Debounces the active-low button and classifies presses
// into Short (quick press+release) and Long (held past kLongMs).
static void pollButton() {
  bool raw = !pcfGetPin(PCF_PIN_KNOB_BTN);  // active-low -> pressed = true
  uint32_t now = millis();
  if (raw != s_btnLastRead) {
    s_btnLastRead = raw;
    s_btnChangeMs = now;
  } else if ((now - s_btnChangeMs) >= kDebounceMs && raw != s_btnStable) {
    s_btnStable = raw;
    if (s_btnStable) {                       // press began
      s_pressStart = now;
      s_longFired = false;
    } else if (!s_longFired) {               // released before long threshold -> Short
      s_event = KnobEvent::Short;
    }
  }
  // Fire Long while still held, for immediate feedback.
  if (s_btnStable && !s_longFired && (now - s_pressStart) >= kLongMs) {
    s_longFired = true;
    s_event = KnobEvent::Long;
  }
}

KnobEvent knobEvent() {
  pollButton();
  KnobEvent e = s_event;
  s_event = KnobEvent::None;
  return e;
}

bool knobPressed() { return knobEvent() == KnobEvent::Short; }

bool knobDown() {
  pollButton();
  return s_btnStable;
}
