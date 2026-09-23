# Vendored QR encoder (`lib/qrcodegen`)

[Project Nayuki's QR Code generator, C edition](https://www.nayuki.io/page/qr-code-generator-library),
**MIT** (`LICENSE.txt`, upstream, unmodified).

## Why it is here

`sonos-button-v3` (`boards/waveshare_s3_lcd147/`) wakes a 172x320 ST7789 to show a QR code — the
Wi-Fi setup AP before provisioning, the device's own `:8080` config page after. That is the whole
screen: one code and five lines of text.

The tree already had a QR encoder, but only reachable one way: LVGL vendors this same Nayuki code
at `src/libs/qrcode/`, gated behind `LV_USE_QRCODE`, which `include/lv_conf.h` enables **only**
under `#ifdef UNIT_JUKEBOX`. Using it would have meant linking LVGL into a button env.

That trade is bad here. LVGL's `LV_MEM_SIZE` pool is 96 KB out of this unit's ~243 KB of internal
heap — the tightest resource on the board — and CLAUDE.md already records pool exhaustion as a UI
**freeze**, not a dropped frame. Paying that for a QR and five text lines, on a unit that draws
about twice a day, is not worth it. So the encoder is vendored standalone and the panel is driven
with raw Arduino_GFX; `-DHEADLESS` and the `-<core/ui/>` exclusion both stay exactly as they are on
the other two buttons.

## Where the copy came from

`.pio/libdeps/sonos-jukebox/lvgl/src/libs/qrcode/{qrcodegen.c,qrcodegen.h,LICENSE.txt}` (LVGL
9.2.x). Taking LVGL's copy rather than a fresh upstream checkout is deliberate: it is the exact
code already proven on hardware here, on the jukebox's Settings link QR.

## What was changed vs that copy

Two things, both purely de-LVGL-ification. No logic was touched.

1. **Includes.** `#include "../../../lvgl.h"` plus `LV_STDBOOL_INCLUDE` / `LV_STDDEF_INCLUDE` /
   `LV_STDINT_INCLUDE` became plain `<stdbool.h>` / `<stddef.h>` / `<stdint.h>`, and
   `#include "../../misc/lv_assert.h"` was dropped.
2. **Assertions.** LVGL had replaced upstream's `assert()` with `LV_ASSERT()`. That is a **no-op in
   LVGL's default configuration**, so a local `#define LV_ASSERT(expr) ((void)0)` reproduces what
   the jukebox already ships, byte for byte in behaviour.

   Deliberately *not* restored to `assert()`. Every one of the ~50 call sites is an
   internal-consistency check on arguments this firmware controls, and `abort()` on a wall-powered
   button with no console is the worst available response to one.

The `#ifdef LV_USE_QRCODE` wrapper (and its closing `#endif`) was removed with it, since nothing
defines that symbol in a non-LVGL env.

## The API, and what the caller needs

```c
uint8_t qr  [qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];
uint8_t tmp [qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VER)];
if (qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM,
                         qrcodegen_VERSION_MIN, QR_MAX_VER,
                         qrcodegen_Mask_AUTO, true)) {
    int n = qrcodegen_getSize(qr);                 // modules per side
    bool dark = qrcodegen_getModule(qr, x, y);     // one fillRect per module
}
```

**Both buffers are sized by the MAXIMUM version you allow, not by the string** — cap
`QR_MAX_VER` rather than passing `qrcodegen_VERSION_MAX`, whose buffers are 3918 bytes each.
`boards/waveshare_s3_lcd147/display.cpp` caps at version 6 (41x41 modules, 176 B/buffer), which
holds ~130 alphanumeric characters at ECC M — far more than either an `http://<ip>:8080` URL or a
`WIFI:` join string needs.
