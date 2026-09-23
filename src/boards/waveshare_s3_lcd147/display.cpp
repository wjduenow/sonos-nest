// See display.h.
#include "display.h"
#include "pins.h"
#include "core/board.h"      // batteryPercent()
#include <Arduino.h>
#include <string.h>
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

// The QR currently ON the glass. A page whose QR text is unchanged repaints only its text column,
// which is what keeps a drifting RSSI from flashing the whole screen — see displayQrPage().
static String s_lastQr;

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
  // Invalidate the cache: the glass has just been cleared, so the next page MUST redraw its QR
  // even if the text is identical. Forgetting this leaves a page with no QR on it at all.
  s_lastQr = "";
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

// A gauge that fills left to right across the whole status column. Full width because the column
// is otherwise mostly empty, and because a long bar resolves a change of a few percent into
// something actually visible — the old 34 px glyph quantised to four bars and threw the rest away.
//
// Still drawn rather than written: a bar reads at a glance from across a room and "72%" does not.
// The number sits inside it for when the glance is not enough.
static void drawBattery(int16_t x, int16_t y, int16_t w, int pct) {
  const int16_t H = 18, NUB = 3;
  const int16_t bw = w - NUB;

  // ⚠️ DARK fills, not Arduino_GFX's GREEN/YELLOW/RED. Those are full-brightness primaries and the
  // percentage is printed ON TOP of the fill, so white-on-GREEN came out barely legible. The text
  // has to sit on a moving background — black where the bar has not reached, the fill where it
  // has — so the only colour that works for it is white, and the fills have to be dark enough to
  // carry white. RGB565, roughly (0,130,40), (200,130,0) and (190,30,30).
  static const uint16_t BAT_OK = 0x0405, BAT_LOW = 0xCC00, BAT_CRIT = 0xB8E3;
  const uint16_t col = pct <= 15 ? BAT_CRIT : (pct <= 35 ? BAT_LOW : BAT_OK);

  s_gfx->drawRect(x, y, bw, H, LIGHTGREY);
  s_gfx->fillRect(x + bw, y + (H - 8) / 2, NUB, 8, LIGHTGREY);

  // The fill is continuous now, not quantised — see the note above. Two pixels of inset so the
  // fill never touches the outline, which at 100% would otherwise look like one solid slab.
  const int16_t fill = (int16_t)((int32_t)(bw - 4) * pct / 100);
  if (fill > 0) s_gfx->fillRect(x + 2, y + 2, fill, H - 4, col);

  // Printed over the fill, so it stays put as the bar moves rather than being pushed along by it.
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  s_gfx->setTextSize(1);
  s_gfx->setTextColor(WHITE);
  s_gfx->setCursor(x + bw / 2 - (int16_t)(strlen(buf) * 3), y + 6);
  s_gfx->print(buf);
}


// Draw prose at `size`, wrapped to the column, breaking at spaces where it can and mid-word only
// when a single word is longer than a line. Returns the y below the last row drawn.
//
// Exists so the provisioning page can be read at double height: that page's whole job is to be
// legible to someone who could not scan the code, and at single height it was too small to be
// that. An SSID wrapping across two rows is worth far more than one that fits but cannot be read.
static int16_t drawWrapped(const char *txt, int16_t x, int16_t y, int16_t w,
                           int16_t bottom, uint8_t size) {
  const int16_t cw = 6 * size, rh = 8 * size + 3;
  const int     maxch = w / cw;
  if (maxch < 1) return y;

  s_gfx->setTextSize(size);
  const char *p = txt;
  while (*p && y + rh <= bottom) {
    int take = 0, lastSpace = -1;
    while (p[take] && take < maxch) {
      if (p[take] == ' ') lastSpace = take;
      ++take;
    }
    // Only break at a space if there is more text after this line — otherwise a final short word
    // gets pushed onto a row of its own for no reason.
    if (p[take] && lastSpace > 0) take = lastSpace;

    s_gfx->setCursor(x, y);
    for (int i = 0; i < take; ++i) s_gfx->write(p[i]);
    y += rh;
    p += take;
    while (*p == ' ') ++p;                       // swallow the break's own space
  }
  return y;
}


void displayQrPage(const char *qrText, const char *caption,
                   const char *const *lines, uint8_t nLines) {
  if (!s_gfx) return;

  // ⚠️ REPAINT ONLY WHAT CHANGED. A full-page repaint starts with fillScreen(BLACK), which on a
  // panel this size is a visible flash — and the status text carries RSSI, which drifts on its own
  // (a real swing of -55 to -67 with the box sitting still). The QR is by far the most expensive
  // and most conspicuous thing here and almost never changes, so it is redrawn only when its
  // CONTENT does. A text-only update clears just the right-hand column.
  const bool qrChanged = (s_lastQr != (qrText ? qrText : ""));

  const int16_t MARGIN = 6;
  // 150, not the full 160 the height allows. The QR's module scale is an INTEGER division of the
  // box by (modules + quiet zone), and at both versions this page ever produces — v2 at 33 and v3
  // at 37 — 150 and 160 both give scale 4. So the 10 px is free: the QR renders pixel-identical
  // and the status column gains a thirteenth character. Do not shrink it further without redoing
  // that arithmetic; 148 is where v3 starts to lose a scale step.
  const int16_t box    = 150;
  // 4, not MARGIN: the status column is the tight one and the QR does not care about a couple of
  // pixels. Those 4 px are what let a 12-character room name render at double height — see the
  // auto-sizing below, where 12 chars is exactly the boundary.
  const int16_t GAP    = 4;
  const int16_t tx     = MARGIN + box + GAP;
  const int16_t tw     = DISP_W - tx - GAP;      // usable width of the status column

  if (qrChanged) {
    s_gfx->fillScreen(BLACK);
    s_lastQr = qrText ? qrText : "";
  } else {
    s_gfx->fillRect(tx, 0, DISP_W - tx, DISP_H, BLACK);   // just the text column
  }
  // NOTE: the built-in GFX font is ASCII-only, and setUTF8Print() needs U8G2_FONT_SUPPORT, which
  // this build does not enable. Callers pass text that has already been folded to ASCII — see
  // asciiFold() in units/sleep_button/screens.cpp, which matters because room names come off the
  // network and "Küche" is a perfectly ordinary one.

  // Left: the QR gets a full-height square. Right: caption over status lines.
  // An EMPTY qrText is a legitimate state, not a failure: before Wi-Fi is up there is no config
  // URL to encode, and the page still has useful things to say on the right. Leave the left half
  // black in that case. Only a genuine encode failure earns the red text — a silently blank half
  // would otherwise read as a dead panel on the one screen whose job is to be readable when
  // something has gone wrong.
  const int16_t qy = (DISP_H - box) / 2;        // centred now the box is not the full height
  if (qrChanged && qrText && *qrText && !drawQr(qrText, MARGIN, qy, box)) {
    s_gfx->setTextColor(RED);
    s_gfx->setTextSize(1);
    s_gfx->setCursor(MARGIN + 4, qy + box / 2 - 4);
    s_gfx->print("QR encode failed");
  }

  int16_t ty = MARGIN;

  // The caption takes the same '#' prefix as a prose line: double height, wrapped. Opt-in rather
  // than always-on because it costs a row the STATUS page cannot spare — a two-row caption plus
  // four label/value items needs 163 px of a 146 px budget, and the fourth item would vanish. The
  // pages that ask for it are the ones with only two or three short lines under them.
  if (caption && *caption) {
    const bool bigCap = (*caption == '#');
    s_gfx->setTextColor(CYAN);
    if (bigCap) {
      ty = drawWrapped(caption + 1, tx, ty, tw, DISP_H - 40, 2);
    } else {
      s_gfx->setTextSize(1);
      s_gfx->setCursor(tx, ty);
      s_gfx->print(caption);
      ty += 12;
    }
    s_gfx->drawFastHLine(tx, ty, tw, DARKGREY);
    ty += 7;
  }

  // Each line arrives as "label\tvalue" and is drawn on TWO rows: a small grey label over a
  // double-height value. That is what fills the column — at one size on one row it was a strip of
  // tiny text with two thirds of the panel black underneath.
  //
  // The value's size is chosen per line, largest that fits: 12 px/char at size 2, 6 at size 1.
  // Adaptive because the values genuinely differ — a room name wants to be readable across a
  // room, while an IP address is 14 characters that nobody reads at a glance anyway, and forcing
  // one size on both either truncates the address or wastes the room name.
  const int16_t BOTTOM = DISP_H - 26;            // leave room for the gauge
  for (uint8_t i = 0; i < nLines && lines; ++i) {
    if (!lines[i]) continue;
    const char *tab = strchr(lines[i], '\t');
    const char *val = tab ? tab + 1 : lines[i];

    // A label beginning '*' marks its value as the ACCENT line — drawn amber rather than white.
    // An explicit marker at the call site rather than "the first line is special": positional
    // magic here would break silently the moment anyone reorders the lines, and the reorder would
    // look entirely reasonable to whoever did it.
    const char *lab = lines[i];
    const bool accent = tab && *lab == '*';
    if (accent) ++lab;
    // ⚠️ ALWAYS DOUBLE HEIGHT, TRUNCATED TO FIT — never dropped to single height. Auto-sizing was
    // tried first and is worse in use: it makes a value's size depend on its length, so a room
    // called "Den" renders twice the height of one called "Dining Room" and the page appears to
    // change layout for no reason the reader can see. A consistently large value that runs out of
    // room is both easier to read and easier to trust.
    const int16_t maxch = tw / 12;                  // 13 at the current column width
    char vbuf[24];
    if ((int16_t)strlen(val) <= maxch) {
      snprintf(vbuf, sizeof(vbuf), "%s", val);
    } else {
      // Keep maxch-1 characters and mark the cut. Without a marker a clipped name reads as a
      // complete one, which on a screen whose job is to say which playlist is mapped is exactly
      // the wrong failure.
      int keep = maxch - 1;
      if (keep > (int)sizeof(vbuf) - 2) keep = (int)sizeof(vbuf) - 2;
      memcpy(vbuf, val, keep);
      vbuf[keep]     = '>';
      vbuf[keep + 1] = '\0';
    }
    // ⚠️ 9 and 3, not 10 and 5. Four double-height items plus the caption and the gauge come to
    // 149 px of a 146 px budget at the looser spacing — the fourth was being silently dropped by
    // the BOTTOM check below, which looks identical to "the unit only sent three".
    // A line with NO tab is prose rather than a label/value pair. A leading '#' asks for it at
    // DOUBLE height, wrapped; plain prose stays single height, where 26 characters against 13
    // means a whole sentence fits on one row. Both forms exist because the two pages want
    // opposite things: the provisioning page needs a few words big enough to read across a room,
    // the no-Wi-Fi page needs four sentences of explanation.
    const bool bigProse = !tab && *lines[i] == '#';
    const int16_t need = tab ? (9 + 16 + 3) : (bigProse ? 19 : 11);
    if (ty + need > BOTTOM) break;               // out of glass; drop the rest silently

    if (tab) {
      s_gfx->setTextSize(1);
      s_gfx->setTextColor(DARKGREY);
      s_gfx->setCursor(tx, ty);
      for (const char *c = lab; c < tab; ++c) s_gfx->write(*c);
      ty += 9;
    }
    // Amber for the room, white for everything else — the room is WHERE this will play and the
    // rest is WHAT, and on a screen read at a glance that distinction is worth a colour.
    static const uint16_t ACCENT = 0xFDE7;       // ~(255,190,60)
    s_gfx->setTextColor(accent ? ACCENT : WHITE);
    if (tab) {
      s_gfx->setTextSize(2);
      s_gfx->setCursor(tx, ty);
      s_gfx->print(vbuf);
      ty += 16 + 3;
    } else {
      // Wrapped prose reports its own height — it may occupy several rows.
      ty = drawWrapped(bigProse ? val + 1 : val, tx, ty, tw, BOTTOM, bigProse ? 2 : 1);
    }
  }

  // Along the bottom, full column width. Skipped entirely when there is no sensing or no cell —
  // an empty outline would read as "flat", which is the opposite of the truth.
  const int bat = batteryPercent();
  if (bat >= 0) drawBattery(tx, DISP_H - 24, tw, bat);
}
