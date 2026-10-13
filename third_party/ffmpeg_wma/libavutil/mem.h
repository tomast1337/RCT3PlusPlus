/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVUTIL_MEM_H
#define RCT3_AVUTIL_MEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static inline void* av_malloc_array(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size)
        return NULL;
    return malloc(count * size);
}

/** Frees *ptr, a pointer to a pointer, and sets it to NULL. */
static inline void av_freep(void* ptr)
{
    void** p = (void**)ptr;
    free(*p);
    *p = NULL;
}

#endif
