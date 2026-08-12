/* Version ID for the JPEG library.
 * Might be useful for tests like "#if JPEG_LIB_VERSION >= 60".
 */
#define JPEG_LIB_VERSION  62

/* libjpeg-turbo version */
#define LIBJPEG_TURBO_VERSION  2.1.5.1

/* libjpeg-turbo version in integer form */
#define LIBJPEG_TURBO_VERSION_NUMBER  2001005

/* Support arithmetic encoding */
/* #undef C_ARITH_CODING_SUPPORTED */

/* Support arithmetic decoding */
/* #undef D_ARITH_CODING_SUPPORTED */

/* Support in-memory source/destination managers */
#define MEM_SRCDST_SUPPORTED 1

/* Use accelerated SIMD routines. */
/* #undef WITH_SIMD */

/*
 * Define BITS_IN_JSAMPLE as either
 *   8   for 8-bit sample values (the usual setting)
 *   12  for 12-bit sample values
 * Only 8 and 12 are legal data precisions for lossy JPEG according to the
 * JPEG standard, and the IJG code does not support anything else!
 * We do not support run-time selection of data precision, sorry.
 */

#define BITS_IN_JSAMPLE  8      /* use 8 or 12 */

/* Define if your (broken) compiler shifts signed values as if they were
   unsigned. */
/* #undef RIGHT_SHIFT_IS_UNSIGNED */

/*
 * *** LOCAL CHANGE (2 of 2 — see ../VENDORING.md). ***
 *
 * Arduino.h has `typedef bool boolean;`, and libjpeg's jmorecfg.h would otherwise declare
 * `typedef int boolean;` — a hard "conflicting declaration" error in any file that includes both,
 * which is every consumer of this library here.
 *
 * Fixing that in the CONSUMER alone would be an ABI split of the kind CLAUDE.md already documents
 * for TFLM: the library's own C files would keep a 4-byte `boolean` inside `jpeg_decompress_struct`
 * while the caller read a 1-byte one, so every field after the first boolean would sit at the wrong
 * offset — and it would still compile and link cleanly. So it is set HERE, in the header that every
 * libjpeg translation unit and every caller includes, which keeps both sides identical by
 * construction.
 *
 * A 1-byte boolean is a configuration upstream already supports: the generated _WIN32 branch uses
 * `unsigned char` for exactly this reason. libjpeg only ever stores TRUE/FALSE in these fields.
 */
#ifndef HAVE_BOOLEAN
#ifndef __cplusplus
#include <stdbool.h>
#endif
typedef bool boolean;
#define HAVE_BOOLEAN
#endif
