// See jpeg_decode.h. Vendored libjpeg-turbo lives in lib/jpegdec (VENDORING.md explains why it is
// vendored, why 2.1.5.1, and what the one local patch is).
#include "jpeg_decode.h"

#include <setjmp.h>

extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}

#include "core/net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "core/heap_watch.h"      // heapwatch::note — attribute the internal-heap low-water

// Source-dimension guard. It is the SOURCE that has to be capped, not the output: a progressive
// image holds its whole coefficient array before emitting a pixel, so cost scales with the input
// (~3 bytes/px at 4:2:0) no matter how small the result is — 1024 px is ~3 MB. Sonos has served
// nothing above 800 px here, so this is headroom, not a target. It exists so a pathological cover
// fails cleanly rather than eating the PSRAM the display buffers live in.
#ifndef JPEG_PROG_MAX_PX
#define JPEG_PROG_MAX_PX 1024
#endif

// libjpeg's default error handler calls exit(). Replace it or a malformed cover takes the device
// down; longjmp back to the caller instead and let it fall through to "no art".
struct JpegErr {
  struct jpeg_error_mgr pub;
  jmp_buf jb;
  char msg[JMSG_LENGTH_MAX];
};

static void jpegOnError(j_common_ptr ci) {
  JpegErr *e = (JpegErr *)ci->err;
  (*ci->err->format_message)(ci, e->msg);
  longjmp(e->jb, 1);
}
static void jpegOnMessage(j_common_ptr) {}   // corrupt-data warnings: kept, not printed

bool jpegIsProgressive(const uint8_t *j, size_t len) {
  if (!j || len < 4 || j[0] != 0xFF || j[1] != 0xD8) return false;
  size_t i = 2;
  while (i + 3 < len) {
    if (j[i] != 0xFF) { ++i; continue; }
    const uint8_t m = j[i + 1];
    if (m == 0xFF) { ++i; continue; }                       // fill byte
    if (m == 0x01 || (m >= 0xD0 && m <= 0xD9)) { i += 2; continue; }   // standalone markers
    if (m == 0xDA) return false;                            // scan data: no frame header found
    // SOF2/6/10 are the progressive frame headers; SOF0/1/9 are the baseline/sequential ones.
    if (m == 0xC2 || m == 0xC6 || m == 0xCA) return true;
    if (m == 0xC0 || m == 0xC1 || m == 0xC3 || m == 0xC5 || m == 0xC7 ||
        m == 0xC9 || m == 0xCB || m == 0xCD || m == 0xCE || m == 0xCF) return false;
    i += 2 + ((size_t)j[i + 2] << 8 | j[i + 3]);            // segment length includes itself
  }
  return false;
}

bool jpegDecodeRgb565(const uint8_t *jpeg, size_t len, uint16_t *dst, int dstStride,
                      int maxW, int maxH, int *outW, int *outH) {
  if (!jpeg || !dst || len < 100 || maxW <= 0 || maxH <= 0) return false;

  struct jpeg_decompress_struct ci;
  JpegErr err;
  ci.err = jpeg_std_error(&err.pub);
  err.pub.error_exit     = jpegOnError;
  err.pub.output_message = jpegOnMessage;
  err.msg[0] = 0;

  if (setjmp(err.jb)) {          // every failure below lands here, including a failed PSRAM alloc
    LOG.printf("[jpeg  ] decode failed: %s\n", err.msg);
    jpeg_destroy_decompress(&ci);
    return false;
  }

  const uint32_t t0 = millis();
  jpeg_create_decompress(&ci);
  jpeg_mem_src(&ci, (unsigned char *)jpeg, (unsigned long)len);
  jpeg_read_header(&ci, TRUE);

  if ((int)ci.image_width > JPEG_PROG_MAX_PX || (int)ci.image_height > JPEG_PROG_MAX_PX) {
    LOG.printf("[jpeg  ] %ux%u exceeds JPEG_PROG_MAX_PX=%d, refusing\n",
               (unsigned)ci.image_width, (unsigned)ci.image_height, JPEG_PROG_MAX_PX);
    jpeg_destroy_decompress(&ci);
    return false;
  }

  // Unlike TJpgDec's powers of two, libjpeg scales by any n/8 — so this lands close under the cap
  // instead of undershooting it to the next power of two (a 500 px cover under a 320 px cap gives
  // 312 here, where TJpgDec gives 250). Walk down from 8/8 and take the first that fits.
  int num = 8;
  for (; num > 1; --num) {
    const int w = ((int)ci.image_width  * num + 7) / 8;
    const int h = ((int)ci.image_height * num + 7) / 8;
    if (w <= maxW && h <= maxH) break;
  }
  ci.scale_num     = num;
  ci.scale_denom   = 8;
  ci.out_color_space = JCS_RGB565;      // straight to the panel format; no RGB888 middle buffer
  ci.dither_mode   = JDITHER_NONE;      // the default is JDITHER_FS, which picks the slower path
  ci.dct_method    = JDCT_ISLOW;

  jpeg_start_decompress(&ci);
  const int w = (int)ci.output_width, h = (int)ci.output_height;
  const int stride = dstStride > 0 ? dstStride : w;
  if (w > maxW || h > maxH || w > stride) {   // 1/8 was still too big, or the caller's stride is short
    LOG.printf("[jpeg  ] %dx%d does not fit %dx%d (stride %d)\n", w, h, maxW, maxH, stride);
    jpeg_abort_decompress(&ci);
    jpeg_destroy_decompress(&ci);
    return false;
  }

  // Rows go straight into the caller's buffer: libjpeg's RGB565 writer tests its own alignment
  // (PACK_NEED_ALIGNMENT in jdcol565.c), so an odd stride is safe, and it packs little-endian —
  // the same byte order TJpgDec produces with setSwapBytes(false).
  while ((int)ci.output_scanline < h) {
    JSAMPROW row = (JSAMPROW)(dst + (size_t)ci.output_scanline * stride);
    if (jpeg_read_scanlines(&ci, &row, 1) != 1) break;
  }
  heapwatch::note("jpeg.progressive");   // while the coefficient array is still held

  // Read everything needed off ci BEFORE destroying it — the log line below outlives the decoder.
  const int rows = (int)ci.output_scanline;
  const unsigned srcW = (unsigned)ci.image_width, srcH = (unsigned)ci.image_height;
  if (rows < h) jpeg_abort_decompress(&ci); else jpeg_finish_decompress(&ci);
  jpeg_destroy_decompress(&ci);

  if (rows < h) { LOG.printf("[jpeg  ] truncated at row %d of %d\n", rows, h); return false; }
  if (outW) *outW = w;
  if (outH) *outH = h;
  LOG.printf("[jpeg  ] %ux%u -> %dx%d (%d/8) in %lu ms\n", srcW, srcH, w, h, num,
             (unsigned long)(millis() - t0));
  return true;
}
