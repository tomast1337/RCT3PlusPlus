/*
 * The WMA decoder cut from FFmpeg, behind a small C interface written for rct3pp. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "rct3_wma.h"

#include <stdlib.h>
#include <string.h>

#include "libavcodec/codec_internal.h"

extern const FFCodec ff_wmav1_decoder;
extern const FFCodec ff_wmav2_decoder;

struct Rct3WmaDecoder {
    const FFCodec* codec;
    AVCodecContext context;
    AVCodecInternal internal;
    AVFrame frame;
    uint8_t* packet; /* One packet and the zeros the decoder may read past it */
    int delay;
};

void rct3_wma_close(Rct3WmaDecoder* d)
{
    if (d == NULL)
        return;
    if (d->context.priv_data != NULL)
        d->codec->close(&d->context);
    free(d->context.priv_data);
    free(d->context.extradata);
    free(d->frame.planes[0]);
    free(d->frame.planes[1]);
    free(d->packet);
    free(d);
}

Rct3WmaDecoder* rct3_wma_open(const Rct3WmaFormat* f)
{
    if (f == NULL || (f->version != 1 && f->version != 2) || f->block_align <= 0 || f->extra_size < 0 || f->extra_size > 64)
        return NULL;
    Rct3WmaDecoder* d = calloc(1, sizeof(*d));
    if (d == NULL)
        return NULL;
    d->codec = f->version == 1 ? &ff_wmav1_decoder : &ff_wmav2_decoder;
    AVCodecContext* c = &d->context;
    c->codec = &d->codec->p;
    c->internal = &d->internal;
    c->sample_rate = f->sample_rate;
    c->ch_layout.nb_channels = f->channels;
    c->bit_rate = f->bit_rate;
    c->block_align = f->block_align;
    c->extradata = calloc(1, (size_t)f->extra_size + AV_INPUT_BUFFER_PADDING_SIZE);
    c->extradata_size = f->extra_size;
    c->priv_data = calloc(1, (size_t)d->codec->priv_data_size);
    d->packet = calloc(1, (size_t)f->block_align + AV_INPUT_BUFFER_PADDING_SIZE);
    if (c->extradata == NULL || c->priv_data == NULL || d->packet == NULL)
    {
        rct3_wma_close(d);
        return NULL;
    }
    if (f->extra_size > 0)
        memcpy(c->extradata, f->extra, (size_t)f->extra_size);
    if (d->codec->init(c) < 0)
    {
        rct3_wma_close(d); /* The decoder cleans up after a failed start itself, as its close does */
        return NULL;
    }
    d->delay = d->internal.skip_samples;
    return d;
}

int rct3_wma_decode(Rct3WmaDecoder* d, const uint8_t* packet, int size, const float* const** planes)
{
    AVPacket p = { d->packet, 0 };
    if (size > 0)
    {
        if (size < d->context.block_align)
            return -1;
        memcpy(d->packet, packet, (size_t)d->context.block_align);
        p.size = d->context.block_align;
    }
    int got = 0;
    d->frame.nb_samples = 0;
    const int ret = d->codec->decode(&d->context, &d->frame, &got, &p);
    if (ret < 0)
        return ret;
    if (!got)
        return 0;
    *planes = (const float* const*)d->frame.planes;
    return d->frame.nb_samples;
}

int rct3_wma_delay(const Rct3WmaDecoder* d)
{
    return d->delay;
}

void rct3_wma_reset(Rct3WmaDecoder* d)
{
    d->codec->flush(&d->context);
}
