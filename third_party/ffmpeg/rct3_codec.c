/*
 * What rct3_wma.c and rct3_wmv.c share. See rct3_codec.h and README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "rct3_codec.h"

#include "libavcodec/internal.h"
#include "libavcodec/refstruct.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"

AVCodecContext* rct3_codec_alloc(const FFCodec* codec)
{
    AVCodecContext* c = av_mallocz(sizeof(*c));
    if (c == NULL)
        return NULL;
    c->av_class = NULL;
    c->codec = &codec->p;
    c->codec_type = codec->p.type;
    c->codec_id = codec->p.id;
    /* The defaults avcodec_alloc_context3 would give the fields these decoders read. */
    c->time_base = (AVRational){ 0, 1 };
    c->framerate = (AVRational){ 0, 1 };
    c->pkt_timebase = (AVRational){ 0, 1 };
    c->sample_aspect_ratio = (AVRational){ 0, 1 };
    c->pix_fmt = AV_PIX_FMT_NONE;
    c->sample_fmt = AV_SAMPLE_FMT_NONE;
    c->color_primaries = AVCOL_PRI_UNSPECIFIED;
    c->color_trc = AVCOL_TRC_UNSPECIFIED;
    c->colorspace = AVCOL_SPC_UNSPECIFIED;
    c->color_range = AVCOL_RANGE_UNSPECIFIED;
    c->chroma_sample_location = AVCHROMA_LOC_UNSPECIFIED;
    c->workaround_bugs = FF_BUG_AUTODETECT;
    c->error_concealment = 3;
    c->thread_count = 1;
    c->profile = AV_PROFILE_UNKNOWN;
    c->level = AV_LEVEL_UNKNOWN;
    c->max_pixels = INT_MAX;
    c->max_samples = INT_MAX;
    c->get_buffer2 = avcodec_default_get_buffer2;
    c->internal = av_mallocz(sizeof(*c->internal));
    if (codec->priv_data_size > 0)
        c->priv_data = av_mallocz((size_t)codec->priv_data_size);
    if (c->internal == NULL || (codec->priv_data_size > 0 && c->priv_data == NULL))
    {
        av_free(c->internal);
        av_free(c->priv_data);
        av_free(c);
        return NULL;
    }
    return c;
}

int rct3_codec_start(AVCodecContext* c)
{
    const FFCodec* codec = ffcodec(c->codec);
    if (c->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        c->coded_width = c->width;
        c->coded_height = c->height;
    }
    int ret = codec->init ? codec->init(c) : 0;
    if (ret < 0)
        return ret;
    c->internal->needs_close = 1;
    return 0;
}

void rct3_codec_free(AVCodecContext** context)
{
    AVCodecContext* c = *context;
    if (c == NULL)
        return;
    const FFCodec* codec = ffcodec(c->codec);
    if (c->internal != NULL && c->internal->needs_close && codec->close)
        codec->close(c);
    if (c->internal != NULL)
        ff_refstruct_unref(&c->internal->pool); /* The frames' buffers, freed with the last of them */
    av_free(c->internal);
    av_free(c->priv_data);
    av_freep(&c->extradata);
    av_channel_layout_uninit(&c->ch_layout);
    av_freep(context);
}

int rct3_codec_decode(AVCodecContext* c, AVPacket* packet, AVFrame* frame)
{
    const FFCodec* codec = ffcodec(c->codec);
    int got = 0;
    const int ret = codec->cb.decode(c, frame, &got, packet);
    if (ret < 0)
        return ret;
    return got ? 1 : 0;
}
