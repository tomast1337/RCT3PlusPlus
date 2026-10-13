/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVCODEC_CODEC_INTERNAL_H
#define RCT3_AVCODEC_CODEC_INTERNAL_H

#include "avcodec.h"

#define FF_CODEC_CAP_INIT_CLEANUP (1 << 1)
#define CODEC_LONG_NAME(str) .p.long_name = (str)
#define FF_CODEC_DECODE_CB(func) .decode = (func)

/** A decoder's entry points, as the decoder files declare them. */
typedef struct FFCodec {
    AVCodec p;
    int priv_data_size;
    int caps_internal;
    int (*init)(AVCodecContext* avctx);
    int (*close)(AVCodecContext* avctx);
    int (*decode)(AVCodecContext* avctx, AVFrame* frame, int* got_frame, AVPacket* packet);
    void (*flush)(AVCodecContext* avctx);
} FFCodec;

#endif
