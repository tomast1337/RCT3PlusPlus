/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVCODEC_AVCODEC_H
#define RCT3_AVCODEC_AVCODEC_H

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FF_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define AV_RL16(p) ((uint16_t)(((const uint8_t*)(p))[0] | ((const uint8_t*)(p))[1] << 8))

#define AVERROR(e) (-(e))
#define AVERROR_INVALIDDATA (-0x41444e49)
#define AVERROR_PATCHWELCOME (-0x45574150)

/** Bytes of zeros the decoder may read past the end of its input. */
#define AV_INPUT_BUFFER_PADDING_SIZE 64
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

#define AV_LOG_ERROR 16
#define AV_LOG_WARNING 24
/** The decoder's messages are dropped: a bad file shows up as an error code. */
#define av_log(ctx, level, ...) ((void)(ctx), (void)(level))
#define ff_tlog(ctx, ...) ((void)0)
#define ff_dlog(ctx, ...) ((void)0)

static inline int av_log2(unsigned v)
{
    return v == 0 ? 0 : 31 - __builtin_clz(v);
}

enum AVMediaType { AVMEDIA_TYPE_AUDIO = 1 };
enum AVCodecID { AV_CODEC_ID_WMAV1 = 0x15007, AV_CODEC_ID_WMAV2 = 0x15008 };
enum AVSampleFormat { AV_SAMPLE_FMT_NONE = -1, AV_SAMPLE_FMT_FLTP = 8 };

#define AV_CODEC_CAP_DR1 (1 << 1)
#define AV_CODEC_CAP_DELAY (1 << 5)
#define AV_CODEC_FLAG_BITEXACT (1 << 23)

typedef struct AVCodec {
    const char* name;
    const char* long_name;
    enum AVMediaType type;
    enum AVCodecID id;
    int capabilities;
    const enum AVSampleFormat* sample_fmts;
} AVCodec;

typedef struct AVChannelLayout {
    int nb_channels;
} AVChannelLayout;

typedef struct AVCodecInternal {
    /** Samples at the start of the stream that are not part of it: the decoder's delay. */
    int skip_samples;
} AVCodecInternal;

typedef struct AVCodecContext {
    void* priv_data;
    const AVCodec* codec;
    AVCodecInternal* internal;
    int sample_rate;
    AVChannelLayout ch_layout;
    int64_t bit_rate;
    int block_align;
    uint8_t* extradata;
    int extradata_size;
    int flags;
    enum AVSampleFormat sample_fmt;
} AVCodecContext;

typedef struct AVFrame {
    int nb_samples;
    uint8_t** extended_data;
    int64_t pts;
    /** Owned by ff_get_buffer: one plane per channel and its size in samples. */
    float* planes[2];
    int capacity;
} AVFrame;

typedef struct AVPacket {
    const uint8_t* data;
    int size;
} AVPacket;

#endif
