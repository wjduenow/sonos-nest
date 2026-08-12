// Progressive-JPEG fallback decoder (lib/jpegdec — vendored libjpeg-turbo).
//
// TJpg_Decoder is baseline-only and stays the fast path everywhere: it streams MCU blocks with a
// few KB of state. Progressive JPEGs — about half of what Amazon Prime Stations serve (issue #16)
// — it rejects at the header, which is what this exists for.
//
// Not a replacement, a fallback. It costs ~3 bytes of PSRAM per SOURCE pixel because progressive
// decode needs the whole coefficient array resident, so calling it for images TJpgDec already
// handles would spend ~760 KB and a few hundred ms for nothing.
#pragma once

#include <Arduino.h>

// True if the buffer's frame header is one TJpgDec cannot decode (SOF2/SOF6/SOF10 — progressive).
// Cheap: it walks marker segments, it does not decode. Use it to log WHY a decode failed; the
// decision to call jpegDecodeRgb565() should ride on TJpgDec having actually failed, so any other
// TJpgDec limitation gets the same second chance.
bool jpegIsProgressive(const uint8_t *jpeg, size_t len);

// Decode into a caller-owned RGB565 buffer, downscaling so the result fits maxW x maxH.
//
//   dst        at least maxW * maxH * 2 bytes
//   dstStride  row stride in PIXELS; 0 means "packed at the decoded width"
//   outW/outH  the decoded size, always <= maxW/maxH
//
// Byte order matches TJpgDec with setSwapBytes(false), i.e. what LVGL's LV_COLOR_FORMAT_RGB565
// expects on little-endian targets — the two paths produce interchangeable buffers.
//
// Returns false and leaves dst untouched on any failure, including a source larger than
// JPEG_PROG_MAX_PX on either edge (the memory guard — see the ⚠ in lib/jpegdec/VENDORING.md for
// why the output cap cannot serve as one).
bool jpegDecodeRgb565(const uint8_t *jpeg, size_t len, uint16_t *dst, int dstStride,
                      int maxW, int maxH, int *outW, int *outH);
