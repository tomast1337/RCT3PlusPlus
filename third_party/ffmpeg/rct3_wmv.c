/*
 * FFmpeg's Windows Media Video 9 (WMV3) decoder, behind a small C interface written for rct3pp. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "rct3_wmv.h"

#include <string.h>

#include "rct3_codec.h"

#include "libavutil/frame.h"
#include "libavutil/mem.h"

extern const FFCodec ff_wmv3_decoder;

struct Rct3WmvDecoder {
    AVCodecContext* context;
    AVFrame* frame;
    uint8_t* data;  /* The frame being decoded and the zeros the decoder may read past it */
    int capacity;
};

void rct3_wmv_close(Rct3WmvDecoder* d)
{
    if (d == NULL)
        return;
    rct3_codec_free(&d->context);
    av_frame_free(&d->frame);
    av_free(d->data);
    av_free(d);
}

Rct3WmvDecoder* rct3_wmv_open(const Rct3WmvFormat* f)
{
    if (f == NULL || f->width <= 0 || f->height <= 0 || f->width > 4096 || f->height > 4096 || f->extra_size < 0 || f->extra_size > 1024)
        return NULL;
    Rct3WmvDecoder* d = av_mallocz(sizeof(*d));
    if (d == NULL)
        return NULL;
    d->context = rct3_codec_alloc(&ff_wmv3_decoder);
    d->frame = av_frame_alloc();
    if (d->context == NULL || d->frame == NULL)
    {
        rct3_wmv_close(d);
        return NULL;
    }
    AVCodecContext* c = d->context;
    c->width = f->width;
    c->height = f->height;
    if (f->extra_size > 0)
    {
        c->extradata = av_mallocz((size_t)f->extra_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (c->extradata == NULL)
        {
            rct3_wmv_close(d);
            return NULL;
        }
        memcpy(c->extradata, f->extra, (size_t)f->extra_size);
        c->extradata_size = f->extra_size;
    }
    if (rct3_codec_start(c) < 0)
    {
        rct3_wmv_close(d);
        return NULL;
    }
    return d;
}

int rct3_wmv_decode(Rct3WmvDecoder* d, const uint8_t* frame, int size, Rct3WmvPicture* picture)
{
    if (size < 0)
        return -1;
    if (size + AV_INPUT_BUFFER_PADDING_SIZE > d->capacity)
    {
        av_free(d->data);
        d->capacity = size + AV_INPUT_BUFFER_PADDING_SIZE;
        d->data = av_malloc((size_t)d->capacity);
        if (d->data == NULL)
        {
            d->capacity = 0;
            return -1;
        }
    }
    if (size > 0)
        memcpy(d->data, frame, (size_t)size);
    memset(d->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    AVPacket p = { 0 };
    p.data = size > 0 ? d->data : NULL;
    p.size = size;
    av_frame_unref(d->frame);
    const int ret = rct3_codec_decode(d->context, &p, d->frame);
    if (ret <= 0)
        return ret;
    if (d->frame->format != AV_PIX_FMT_YUV420P)
        return -1;
    picture->width = d->frame->width;
    picture->height = d->frame->height;
    for (int i = 0; i < 3; ++i)
    {
        picture->planes[i] = d->frame->data[i];
        picture->strides[i] = d->frame->linesize[i];
    }
    return 1;
}
