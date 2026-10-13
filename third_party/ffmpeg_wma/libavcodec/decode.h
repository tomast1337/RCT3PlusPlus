/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVCODEC_DECODE_H
#define RCT3_AVCODEC_DECODE_H

#include "avcodec.h"

/** Makes frame->extended_data hold frame->nb_samples floats for each channel. */
int ff_get_buffer(AVCodecContext* avctx, AVFrame* frame, int flags);

#endif
