/*
 * FFmpeg's Windows Media Audio decoder, behind a small C interface written for rct3pp. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "rct3_wma.h"

#include <string.h>

#include "rct3_codec.h"

#include "libavcodec/internal.h"
#include "libavutil/channel_layout.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

extern const FFCodec ff_wmav1_decoder;
extern const FFCodec ff_wmav2_decoder;

struct Rct3WmaDecoder {
    AVCodecContext* context;
    AVFrame* frame;
    uint8_t* packet; /* One packet and the zeros the decoder may read past it */
    int delay;
};

void rct3_wma_close(Rct3WmaDecoder* d)
{
    if (d == NULL)
        return;
    rct3_codec_free(&d->context);
    av_frame_free(&d->frame);
    av_free(d->packet);
    av_free(d);
}

Rct3WmaDecoder* rct3_wma_open(const Rct3WmaFormat* f)
{
    if (f == NULL || (f->version != 1 && f->version != 2) || f->block_align <= 0 || f->extra_size < 0 || f->extra_size > 64 || f->channels < 1 || f->channels > 2)
        return NULL;
    Rct3WmaDecoder* d = av_mallocz(sizeof(*d));
    if (d == NULL)
        return NULL;
    d->context = rct3_codec_alloc(f->version == 1 ? &ff_wmav1_decoder : &ff_wmav2_decoder);
    d->frame = av_frame_alloc();
    d->packet = av_mallocz((size_t)f->block_align + AV_INPUT_BUFFER_PADDING_SIZE);
    if (d->context == NULL || d->frame == NULL || d->packet == NULL)
    {
        rct3_wma_close(d);
        return NULL;
    }
    AVCodecContext* c = d->context;
    c->sample_rate = f->sample_rate;
    av_channel_layout_default(&c->ch_layout, f->channels);
    c->bit_rate = f->bit_rate;
    c->block_align = f->block_align;
    if (f->extra_size > 0)
    {
        c->extradata = av_mallocz((size_t)f->extra_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (c->extradata == NULL)
        {
            rct3_wma_close(d);
            return NULL;
        }
        memcpy(c->extradata, f->extra, (size_t)f->extra_size);
        c->extradata_size = f->extra_size;
    }
    if (rct3_codec_start(c) < 0 || c->sample_fmt != AV_SAMPLE_FMT_FLTP)
    {
        rct3_wma_close(d);
        return NULL;
    }
    d->delay = c->internal->skip_samples;
    return d;
}

int rct3_wma_decode(Rct3WmaDecoder* d, const uint8_t* packet, int size, const float* const** planes)
{
    AVPacket p = { 0 };
    p.data = d->packet;
    if (size > 0)
    {
        if (size < d->context->block_align)
            return -1;
        memcpy(d->packet, packet, (size_t)d->context->block_align);
        p.size = d->context->block_align;
    }
    av_frame_unref(d->frame);
    const int ret = rct3_codec_decode(d->context, &p, d->frame);
    if (ret <= 0)
        return ret;
    *planes = (const float* const*)d->frame->extended_data;
    return d->frame->nb_samples;
}

int rct3_wma_delay(const Rct3WmaDecoder* d)
{
    return d->delay;
}

void rct3_wma_reset(Rct3WmaDecoder* d)
{
    ffcodec(d->context->codec)->flush(d->context);
}
