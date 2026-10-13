/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVCODEC_PUT_BITS_H
#define RCT3_AVCODEC_PUT_BITS_H

/** Used by FFmpeg's WMA encoder only; here so the shared context compiles. */
typedef struct PutBitContext {
    int unused;
} PutBitContext;

#endif
