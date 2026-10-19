/*
 * What rct3_wma.c and rct3_wmv.c share: a decoder's context set up and driven the way libavcodec's own
 * avcodec_open2() and decode loop would, for the few decoders and settings rct3pp uses. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_CODEC_H
#define RCT3_CODEC_H

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_internal.h"

/* A context for `codec` with libavcodec's defaults for what these decoders read; its private data and
 * internal state allocated. The caller fills in the stream's format, then calls rct3_codec_start. */
AVCodecContext* rct3_codec_alloc(const FFCodec* codec);
/* Starts the decoder; frees nothing on failure. */
int rct3_codec_start(AVCodecContext* context);
void rct3_codec_free(AVCodecContext** context);
/* One packet in, at most one frame out: 1 with a frame, 0 without, negative for a bad packet. */
int rct3_codec_decode(AVCodecContext* context, AVPacket* packet, AVFrame* frame);

#endif
