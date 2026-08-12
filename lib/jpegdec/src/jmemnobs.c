/*
 * jmemnobs.c
 *
 * This file was part of the Independent JPEG Group's software:
 * Copyright (C) 1992-1996, Thomas G. Lane.
 * libjpeg-turbo Modifications:
 * Copyright (C) 2017-2018, D. R. Commander.
 * For conditions of distribution and use, see the accompanying README.ijg
 * file.
 *
 * This file provides a really simple implementation of the system-
 * dependent portion of the JPEG memory manager.  This implementation
 * assumes that no backing-store files are needed: all required space
 * can be obtained from malloc().
 * This is very portable in the sense that it'll compile on almost anything,
 * but you'd better have lots of main memory (or virtual memory) if you want
 * to process big images.
 */

#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jmemsys.h"            /* import the system-dependent declarations */

/*
 * *** THE ONLY LOCAL MODIFICATION TO THE VENDORED SOURCE. *** See ../VENDORING.md.
 *
 * Every allocation is redirected to PSRAM. This is not a tuning choice, it is the whole reason
 * progressive JPEG is affordable on these boards: a progressive image needs its entire coefficient
 * array resident before the first pixel comes out, which measures ~3 bytes per source pixel at
 * 4:2:0 — 762 KB peak / 480 KB in ONE block for a 488x488 cover. Internal SRAM on the jukebox runs
 * ~97 KB free with a largest block near 32 KB, so a single stock malloc() here fails outright.
 * PSRAM has ~30 MB free and is what CLAUDE.md already directs every large buffer to.
 *
 * Note jpeg_get_small() is redirected too, even though those allocations are a few hundred bytes.
 * They are per-decode and there is no reason to spend the scarce heap on them, and keeping both on
 * one allocator means the free path cannot be mismatched.
 */
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define JPEG_MALLOC(n)  heap_caps_malloc((n), MALLOC_CAP_SPIRAM)
#define JPEG_FREE(p)    heap_caps_free(p)
#else
#define JPEG_MALLOC(n)  malloc(n)
#define JPEG_FREE(p)    free(p)
#endif


/*
 * Memory allocation and freeing are controlled by the regular library
 * routines malloc() and free().
 */

GLOBAL(void *)
jpeg_get_small(j_common_ptr cinfo, size_t sizeofobject)
{
  return (void *)JPEG_MALLOC(sizeofobject);
}

GLOBAL(void)
jpeg_free_small(j_common_ptr cinfo, void *object, size_t sizeofobject)
{
  JPEG_FREE(object);
}


/*
 * "Large" objects are treated the same as "small" ones.
 */

GLOBAL(void *)
jpeg_get_large(j_common_ptr cinfo, size_t sizeofobject)
{
  return (void *)JPEG_MALLOC(sizeofobject);
}

GLOBAL(void)
jpeg_free_large(j_common_ptr cinfo, void *object, size_t sizeofobject)
{
  JPEG_FREE(object);
}


/*
 * This routine computes the total memory space available for allocation.
 */

GLOBAL(size_t)
jpeg_mem_available(j_common_ptr cinfo, size_t min_bytes_needed,
                   size_t max_bytes_needed, size_t already_allocated)
{
  if (cinfo->mem->max_memory_to_use) {
    if ((size_t)cinfo->mem->max_memory_to_use > already_allocated)
      return cinfo->mem->max_memory_to_use - already_allocated;
    else
      return 0;
  } else {
    /* Here we always say, "we got all you want bud!" */
    return max_bytes_needed;
  }
}


/*
 * Backing store (temporary file) management.
 * Since jpeg_mem_available always promised the moon,
 * this should never be called and we can just error out.
 */

GLOBAL(void)
jpeg_open_backing_store(j_common_ptr cinfo, backing_store_ptr info,
                        long total_bytes_needed)
{
  ERREXIT(cinfo, JERR_NO_BACKING_STORE);
}


/*
 * These routines take care of any system-dependent initialization and
 * cleanup required.  Here, there isn't any.
 */

GLOBAL(long)
jpeg_mem_init(j_common_ptr cinfo)
{
  return 0;                     /* just set max_memory_to_use to 0 */
}

GLOBAL(void)
jpeg_mem_term(j_common_ptr cinfo)
{
  /* no work */
}
