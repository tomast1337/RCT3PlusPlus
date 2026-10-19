/*
 * The parts of libavcodec and libavutil that the WMA and WMV3 decoders call outside their own files, written
 * small for rct3pp in place of FFmpeg's general versions: one thread, no hardware decoding, no logging,
 * planar 4:2:0 pictures and plain mono or stereo sound only. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_internal.h"
#include "libavcodec/dct.h"
#include "libavcodec/decode.h"
#include "libavcodec/faanidct.h"
#include "libavcodec/hwaccel_internal.h"
#include "libavcodec/internal.h"
#include "libavcodec/me_cmp.h"
#include "libavcodec/simple_idct.h"
#include "libavcodec/thread.h"
#include "libavcodec/threadframe.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/dict.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/tx_priv.h"
#include "libavutil/video_enc_params.h"

/* Logging: the decoders' messages are dropped. */

void av_log(void* avcl, int level, const char* fmt, ...)
{
    (void)avcl;
    (void)level;
    (void)fmt;
}

void avpriv_request_sample(void* avc, const char* msg, ...)
{
    (void)avc;
    (void)msg;
}

void avpriv_report_missing_feature(void* avc, const char* msg, ...)
{
    (void)avc;
    (void)msg;
}

const char* av_default_item_name(void* ctx)
{
    return (*(AVClass**)ctx)->class_name;
}

/* Only the decoders' debug output prints into these; it is dropped, and what it builds is empty. */

void av_bprint_init(AVBPrint* buf, unsigned size_init, unsigned size_max)
{
    (void)size_init;
    (void)size_max;
    buf->str = buf->reserved_internal_buffer;
    buf->len = 0;
    buf->size = 1;
    buf->size_max = 1;
    buf->str[0] = 0;
}

void av_bprintf(AVBPrint* buf, const char* fmt, ...)
{
    (void)buf;
    (void)fmt;
}

void av_bprint_chars(AVBPrint* buf, char c, unsigned n)
{
    (void)buf;
    (void)c;
    (void)n;
}

void av_bprint_clear(AVBPrint* buf)
{
    buf->len = 0;
}

int av_bprint_finalize(AVBPrint* buf, char** ret_str)
{
    (void)buf;
    if (ret_str != NULL)
    {
        *ret_str = av_mallocz(1);
        return *ret_str ? 0 : AVERROR(ENOMEM);
    }
    return 0;
}

char av_get_picture_type_char(enum AVPictureType pict_type)
{
    switch (pict_type)
    {
    case AV_PICTURE_TYPE_I: return 'I';
    case AV_PICTURE_TYPE_P: return 'P';
    case AV_PICTURE_TYPE_B: return 'B';
    case AV_PICTURE_TYPE_S: return 'S';
    case AV_PICTURE_TYPE_SI: return 'i';
    case AV_PICTURE_TYPE_SP: return 'p';
    case AV_PICTURE_TYPE_BI: return 'b';
    default: return '?';
    }
}

int av_get_cpu_flags(void)
{
    return 0; /* Plain C everywhere */
}

/* Nothing here sets a frame's metadata, so there is never a dictionary to copy. */

void av_dict_free(AVDictionary** m)
{
    *m = NULL;
}

int av_dict_copy(AVDictionary** dst, const AVDictionary* src, int flags)
{
    (void)dst;
    (void)flags;
    return src == NULL ? 0 : AVERROR(ENOSYS);
}

/* No hardware frames. */

int av_hwframe_get_buffer(AVBufferRef* hwframe_ctx, AVFrame* frame, int flags)
{
    (void)hwframe_ctx;
    (void)frame;
    (void)flags;
    return AVERROR(ENOSYS);
}

int av_hwframe_transfer_data(AVFrame* dst, const AVFrame* src, int flags)
{
    (void)dst;
    (void)src;
    (void)flags;
    return AVERROR(ENOSYS);
}

int ff_hwaccel_frame_priv_alloc(AVCodecContext* avctx, void** hwaccel_picture_private)
{
    (void)avctx;
    *hwaccel_picture_private = NULL;
    return 0;
}

void av_fast_padded_malloc(void* ptr, unsigned int* size, size_t min_size)
{
    uint8_t** p = ptr;
    if (min_size > SIZE_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
    {
        av_freep(p);
        *size = 0;
        return;
    }
    av_fast_mallocz(p, size, min_size + AV_INPUT_BUFFER_PADDING_SIZE);
}

/* Encoder parameters are never asked for as side data. */
AVVideoEncParams* av_video_enc_params_create_side_data(AVFrame* frame, enum AVVideoEncParamsType type, unsigned int nb_blocks)
{
    (void)frame;
    (void)type;
    (void)nb_blocks;
    return NULL;
}

/* Pictures: planar 4:2:0, eight bits, is the only format the decoders are asked for. */

static const AVPixFmtDescriptor yuv420p = {
    .name = "yuv420p",
    .nb_components = 3,
    .log2_chroma_w = 1,
    .log2_chroma_h = 1,
    .flags = AV_PIX_FMT_FLAG_PLANAR,
    .comp = {
        { .plane = 0, .step = 1, .depth = 8 },
        { .plane = 1, .step = 1, .depth = 8 },
        { .plane = 2, .step = 1, .depth = 8 },
    },
};

const AVPixFmtDescriptor* av_pix_fmt_desc_get(enum AVPixelFormat pix_fmt)
{
    return pix_fmt == AV_PIX_FMT_YUV420P ? &yuv420p : NULL;
}

const char* av_get_pix_fmt_name(enum AVPixelFormat pix_fmt)
{
    return pix_fmt == AV_PIX_FMT_YUV420P ? yuv420p.name : NULL;
}

/* Only filling a picture with a colour writes pixel by pixel, and nothing here does. */
void av_write_image_line2(const void* src, uint8_t* data[4], const int linesize[4], const AVPixFmtDescriptor* desc, int x, int y, int c, int w, int src_element_size)
{
    (void)src;
    (void)data;
    (void)linesize;
    (void)desc;
    (void)x;
    (void)y;
    (void)c;
    (void)w;
    (void)src_element_size;
    abort();
}

int av_pix_fmt_count_planes(enum AVPixelFormat pix_fmt)
{
    return pix_fmt == AV_PIX_FMT_YUV420P ? 3 : AVERROR(EINVAL);
}

int av_pix_fmt_get_chroma_sub_sample(enum AVPixelFormat pix_fmt, int* h_shift, int* v_shift)
{
    if (pix_fmt != AV_PIX_FMT_YUV420P)
        return AVERROR(ENOSYS);
    *h_shift = 1;
    *v_shift = 1;
    return 0;
}

void avcodec_align_dimensions2(AVCodecContext* s, int* width, int* height, int linesize_align[AV_NUM_DATA_POINTERS])
{
    /* Whole macroblocks, two high for interlaced pictures, and two rows more: chroma prediction reads one
     * past the bottom. */
    *width = FFALIGN(*width, 16);
    *height = FFALIGN(*height, 32) + 2;
    if (s->codec_id == AV_CODEC_ID_WMV3)
        *width = FFMAX(*width, 32);
    for (int i = 0; i < 4; i++)
        linesize_align[i] = STRIDE_ALIGN;
    for (int i = 4; i < AV_NUM_DATA_POINTERS; i++)
        linesize_align[i] = 1;
}

int ff_set_dimensions(AVCodecContext* s, int width, int height)
{
    const int ret = av_image_check_size2(width, height, s->max_pixels, AV_PIX_FMT_NONE, 0, s);
    if (ret < 0)
        width = height = 0;
    s->coded_width = s->width = width;
    s->coded_height = s->height = height;
    return ret;
}

int ff_set_sar(AVCodecContext* avctx, AVRational sar)
{
    const int ret = av_image_check_sar(avctx->width, avctx->height, sar);
    avctx->sample_aspect_ratio = ret < 0 ? (AVRational){ 0, 1 } : sar;
    return ret < 0 ? ret : 0;
}

int ff_get_format(AVCodecContext* avctx, const enum AVPixelFormat* fmt)
{
    /* No hardware: the software format, which comes last. */
    int n = 0;
    while (fmt[n] != AV_PIX_FMT_NONE)
        n++;
    if (n == 0 || av_pix_fmt_desc_get(fmt[n - 1]) == NULL)
        return AV_PIX_FMT_NONE;
    avctx->sw_pix_fmt = fmt[n - 1];
    return fmt[n - 1];
}

int ff_get_buffer(AVCodecContext* avctx, AVFrame* frame, int flags)
{
    int ret;
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        if ((unsigned)avctx->width > INT_MAX - STRIDE_ALIGN || avctx->pix_fmt < 0 ||
            av_image_check_size2(FFALIGN(avctx->width, STRIDE_ALIGN), avctx->height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx) < 0)
            return AVERROR(EINVAL);
        frame->width = FFMAX(avctx->width, avctx->coded_width);
        frame->height = FFMAX(avctx->height, avctx->coded_height);
        frame->format = avctx->pix_fmt;
        frame->sample_aspect_ratio = avctx->sample_aspect_ratio;
        avctx->sw_pix_fmt = avctx->pix_fmt;
    }
    else
    {
        if (frame->nb_samples * (int64_t)avctx->ch_layout.nb_channels > avctx->max_samples)
            return AVERROR(EINVAL);
        frame->format = avctx->sample_fmt;
        frame->sample_rate = avctx->sample_rate;
        if ((ret = av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout)) < 0)
            return ret;
    }
    ret = avctx->get_buffer2(avctx, frame, flags);
    if (ret < 0)
    {
        av_frame_unref(frame);
        return ret;
    }
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        /* Allocated at the coded size; shown at the display size. */
        frame->width = avctx->width;
        frame->height = avctx->height;
    }
    return 0;
}

/* One thread. */

enum ThreadingStatus ff_thread_sync_ref(AVCodecContext* avctx, size_t offset)
{
    (void)avctx;
    (void)offset;
    return FF_THREAD_NO_FRAME_THREADING;
}

int ff_thread_get_buffer(AVCodecContext* avctx, AVFrame* f, int flags)
{
    return ff_get_buffer(avctx, f, flags);
}

int ff_thread_can_start_frame(AVCodecContext* avctx)
{
    (void)avctx;
    return 1;
}

void ff_thread_await_progress(const ThreadFrame* f, int progress, int field)
{
    (void)f;
    (void)progress;
    (void)field;
}

/* Bitstream start codes: 00 00 01 and one byte more. Returns just past the first in [p, end), keeping the
 * last four bytes read in *state so a code split across two calls is found. */
const uint8_t* avpriv_find_start_code(const uint8_t* restrict p, const uint8_t* end, uint32_t* restrict state)
{
    while (p < end)
    {
        const uint32_t before = *state;
        *state = (before << 8) | *p++;
        if ((before & 0xffffff) == 0x000001)
            return p;
    }
    return end;
}

/* Error concealment compares 16 by 16 blocks by the sum of their differences. */

static int sad16(struct MpegEncContext* v, const uint8_t* a, const uint8_t* b, ptrdiff_t stride, int h)
{
    (void)v;
    int sum = 0;
    for (int y = 0; y < h; y++, a += stride, b += stride)
        for (int x = 0; x < 16; x++)
            sum += abs(a[x] - b[x]);
    return sum;
}

av_cold void ff_me_cmp_init(MECmpContext* c, AVCodecContext* avctx)
{
    (void)avctx;
    memset(c, 0, sizeof(*c));
    c->sad[0] = sad16;
}

/* The inverse transforms one may choose instead of the default: nothing here chooses, so all of them are the
 * default one. Reduced resolution is never asked for, so the smaller ones are never called. */

void ff_j_rev_dct(int16_t* data)
{
    ff_simple_idct_int16_8bit(data);
}

void ff_jref_idct_put(uint8_t* dest, ptrdiff_t line_size, int16_t* block)
{
    ff_simple_idct_put_int16_8bit(dest, line_size, block);
}

void ff_jref_idct_add(uint8_t* dest, ptrdiff_t line_size, int16_t* block)
{
    ff_simple_idct_add_int16_8bit(dest, line_size, block);
}

void ff_faanidct(int16_t block[64])
{
    ff_simple_idct_int16_8bit(block);
}

void ff_faanidct_put(uint8_t* dest, ptrdiff_t line_size, int16_t block[64])
{
    ff_simple_idct_put_int16_8bit(dest, line_size, block);
}

void ff_faanidct_add(uint8_t* dest, ptrdiff_t line_size, int16_t block[64])
{
    ff_simple_idct_add_int16_8bit(dest, line_size, block);
}

void ff_j_rev_dct4(int16_t* data)
{
    (void)data;
    abort();
}

void ff_j_rev_dct2(int16_t* data)
{
    (void)data;
    abort();
}

void ff_j_rev_dct1(int16_t* data)
{
    (void)data;
    abort();
}

/* Sound: the channels in their plain order only, mono or stereo as the files have. */

void av_channel_layout_uninit(AVChannelLayout* layout)
{
    memset(layout, 0, sizeof(*layout));
}

int av_channel_layout_copy(AVChannelLayout* dst, const AVChannelLayout* src)
{
    if (src->order == AV_CHANNEL_ORDER_CUSTOM)
        return AVERROR(ENOSYS);
    *dst = *src;
    return 0;
}

void av_channel_layout_default(AVChannelLayout* layout, int nb_channels)
{
    if (nb_channels == 1)
        *layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    else if (nb_channels == 2)
        *layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    else
        *layout = (AVChannelLayout){ .order = AV_CHANNEL_ORDER_UNSPEC, .nb_channels = nb_channels };
}

int av_channel_layout_check(const AVChannelLayout* layout)
{
    if (layout->nb_channels <= 0)
        return 0;
    if (layout->order == AV_CHANNEL_ORDER_NATIVE)
        return av_popcount64(layout->u.mask) == layout->nb_channels;
    return layout->order == AV_CHANNEL_ORDER_UNSPEC;
}

int av_channel_layout_compare(const AVChannelLayout* a, const AVChannelLayout* b)
{
    if (a->order != b->order || a->nb_channels != b->nb_channels)
        return 1;
    if (a->order == AV_CHANNEL_ORDER_NATIVE && a->u.mask != b->u.mask)
        return 1;
    return a->order == AV_CHANNEL_ORDER_CUSTOM ? -1 : 0;
}

/* The transforms come in float only: the decoders ask for no others. */

const FFTXCodelet* const ff_tx_codelet_list_double_c[] = { NULL };
const FFTXCodelet* const ff_tx_codelet_list_int32_c[] = { NULL };
