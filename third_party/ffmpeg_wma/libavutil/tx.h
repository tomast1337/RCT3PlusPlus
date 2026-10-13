/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVUTIL_TX_H
#define RCT3_AVUTIL_TX_H

#include <stddef.h>
#include <stdint.h>

/** An inverse MDCT of one length, the only transform the decoder needs. */
typedef struct AVTXContext AVTXContext;

/** Transforms `in` into `out`; `stride` is the distance between input values in bytes. */
typedef void (*av_tx_fn)(AVTXContext* s, void* out, void* in, ptrdiff_t stride);

enum AVTXType { AV_TX_FLOAT_MDCT = 1 };
enum AVTXFlags { AV_TX_FULL_IMDCT = 1 << 2 };

/**
 * Sets up an inverse MDCT of `len` coefficients giving 2 * len samples, scaled by *(const float*)scale.
 * Only type AV_TX_FLOAT_MDCT, inv 1 and flag AV_TX_FULL_IMDCT, with len a power of two of at least 16.
 */
int av_tx_init(AVTXContext** ctx, av_tx_fn* tx, enum AVTXType type, int inv, int len, const void* scale, uint64_t flags);
void av_tx_uninit(AVTXContext** ctx);

#endif
