// See display.h.
#include "display.h"
#include "pins.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

extern "C" {
#include "qrcodegen.h"          // lib/qrcodegen — Nayuki's, de-LVGL-ified. See its VENDORING.md.
}

// --- Backlight (LEDC) --------------------------------------------------------------------
// Arduino 2.0.17 API, matching the pinned platform. Kept as the ESP_ARDUINO_VERSION shim the other
// SPI board uses, so this file is not the one that breaks if the platform pin is ever moved.
static const uint32_t kBlFreq = 5000;
static const uint8_t  kBlBits = 8;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  static inline void blAttach() { ledcAttach(PIN_LCD_BL, kBlFreq, kBlBits); }
  static inline void blWrite(uint32_t duty) { ledcWrite(PIN_LCD_BL, duty); }
#else
  static const uint8_t kBlCh = 1;   // ch 0 belongs to the button RING (board.cpp) — do not collide
  static inline void blAttach() {
    ledcSetup(kBlCh, kBlFreq, kBlBits);
    ledcAttachPin(PIN_LCD_BL, kBlCh);
  }
  static inline void blWrite(uint32_t duty) { ledcWrite(kBlCh, duty); }
#endif

// --- Panel -------------------------------------------------------------------------------
static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;

// Logical size after rotation. At rotation 1/3 the panel is landscape, so 320x172.
#if (DISPLAY_ROTATION & 1)
  static const int16_t DISP_W = LCD_HEIGHT, DISP_H = LCD_WIDTH;
#else
  static const int16_t DISP_W = LCD_WIDTH,  DISP_H = LCD_HEIGHT;
#endif

// --- QR ----------------------------------------------------------------------------------
// Version 6 = 41x41 modules, which holds ~130 alphanumeric characters at ECC M. Both an
// `http://192.168.68.123:8080` URL (~26) and a `WIFI:T:nopass;S:...;;` join string (~40) sit well
// inside that, with room for a renamed device.
//
// The CAP is what sizes the buffers, not the string — qrcodegen_BUFFER_LEN_FOR_VERSION(6) is 212
// bytes, against 3918 for VERSION_MAX. That is the whole reason for capping rather than passing
// qrcodegen_VERSION_MAX and letting it pick.
static const int QR_MAX_VER = 6;

// Static, not stack: two 212-byte buffers plus qrcodegen's own frame would be a poor thing to put
// on uiTask's stack. Safe because there is exactly one caller at a time — displayQrPage() runs
// either from uiProvisioning() (before any task exists) or from uiTick(), never both.
static uint8_t s_qr [qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];
static uint8_t s_tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];

bool displayInit() {
  blAttach();
  blWrite(0);                       // stay dark through init — the unit decides when to light up

  s_bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI,
                               GFX_NOT_DEFINED /* MISO — write-only panel */);
  if (!s_bus) return false;

  // The two offsets are the whole reason this panel needs a thought: 172 columns of glass inside
  // 240 columns of controller RAM. Both col_offset arguments get the same value and both
  // row_offsets get 0 — that is deliberate and rotation-proof; see the note in pins.h.
  s_gfx = new Arduino_ST7789(s_bus, PIN_LCD_RST, DISPLAY_ROTATION, LCD_INVERT_COLORS /* ips */,
                             LCD_WIDTH, LCD_HEIGHT,
                             LCD_COL_OFFSET, LCD_ROW_OFFSET, LCD_COL_OFFSET, LCD_ROW_OFFSET);
  if (!s_gfx) return false;

  s_gfx->begin();                   // returns void in Arduino_GFX 1.3.1 — nothing to check
  s_gfx->fillScreen(BLACK);
  return true;
}

void displayBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  blWrite((uint32_t)pct * 255 / 100);
}

void displayBlank() {
  if (s_gfx) s_gfx->fillScreen(BLACK);
  blWrite(0);
}

// Draw the QR into a square of side `box` whose top-left is (x0, y0). Returns false if the text
// would not fit QR_MAX_VER, in which case nothing is drawn.
static bool drawQr(const char *text, int16_t x0, int16_t y0, int16_t box) {
  if (!text || !*text) return false;
  if (!qrcodegen_encodeText(text, s_tmp, s_qr, qrcodegen_Ecc_MEDIUM,
                            qrcodegen_VERSION_MIN, QR_MAX_VER, qrcodegen_Mask_AUTO, true))
    return false;

  const int n = qrcodegen_getSize(s_qr);            // modules per side

  // The quiet zone is not decoration — most scanners will not lock on without ~4 modules of light
  // border, and this page has a BLACK background, so it has to be painted, not just left blank.
  const int QUIET = 4;
  const int scale = box / (n + 2 * QUIET);
  if (scale < 1) return false;                      // box too small for this version

  const int side = (n + 2 * QUIET) * scale;
  const int16_t px = x0 + (box - side) / 2;
  const int16_t py = y0 + (box - side) / 2;

  s_gfx->fillRect(px, py, side, side, WHITE);       // quiet zone + light modules in one go
  const int16_t ox = px + QUIET * scale;
  const int16_t oy = py + QUIET * scale;
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x)
      if (qrcodegen_getModule(s_qr, x, y))
        s_gfx->fillRect(ox + x * scale, oy + y * scale, scale, scale, BLACK);

  return true;
}

void displayQrPage(const char *qrText, const char *caption,
                   const char *const *lines, uint8_t nLines) {
  if (!s_gfx) return;

  s_gfx->fillScreen(BLACK);
  // NOTE: the built-in GFX font is ASCII-only, and setUTF8Print() needs U8G2_FONT_SUPPORT, which
  // this build does not enable. Callers pass text that has already been folded to ASCII — see
  // asciiFold() in units/sleep_button/screens.cpp, which matters because room names come off the
  // network and "Küche" is a perfectly ordinary one.

  // Left: the QR gets a full-height square. Right: caption over status lines.
  const int16_t MARGIN = 6;
  const int16_t box    = DISP_H - 2 * MARGIN;

  // An EMPTY qrText is a legitimate state, not a failure: before Wi-Fi is up there is no config
  // URL to encode, and the page still has useful things to say on the right. Leave the left half
  // black in that case. Only a genuine encode failure earns the red text — a silently blank half
  // would otherwise read as a dead panel on the one screen whose job is to be readable when
  // something has gone wrong.
  if (qrText && *qrText && !drawQr(qrText, MARGIN, MARGIN, box)) {
    s_gfx->setTextColor(RED);
    s_gfx->setTextSize(1);
    s_gfx->setCursor(MARGIN + 4, MARGIN + box / 2 - 4);
    s_gfx->print("QR encode failed");
  }

  const int16_t tx = MARGIN + box + MARGIN;
  int16_t       ty = MARGIN + 4;

  if (caption && *caption) {
    s_gfx->setTextColor(CYAN);
    s_gfx->setTextSize(1);
    s_gfx->setCursor(tx, ty);
    s_gfx->print(caption);
    ty += 14;
    s_gfx->drawFastHLine(tx, ty, DISP_W - tx - MARGIN, DARKGREY);
    ty += 8;
  }

  s_gfx->setTextColor(LIGHTGREY);
  s_gfx->setTextSize(1);
  for (uint8_t i = 0; i < nLines && lines; ++i) {
    if (ty > DISP_H - 10) break;                    // ran out of glass; drop the rest silently
    if (!lines[i]) continue;
    s_gfx->setCursor(tx, ty);
    s_gfx->print(lines[i]);
    ty += 12;
  }
}
