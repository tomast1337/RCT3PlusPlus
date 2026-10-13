/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: the few FFmpeg functions the decoder calls, written for
 * rct3pp in plain C. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/decode.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/sinewin.h"
#include "libavutil/float_dsp.h"
#include "libavutil/tx.h"

/* Vector operations */

static void vector_fmul_add(float* dst, const float* src0, const float* src1, const float* src2, int len)
{
    for (int i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i] + src2[i];
}

static void vector_fmul_reverse(float* dst, const float* src0, const float* src1, int len)
{
    src1 += len - 1;
    for (int i = 0; i < len; i++)
        dst[i] = src0[i] * src1[-i];
}

static void butterflies_float(float* v1, float* v2, int len)
{
    for (int i = 0; i < len; i++)
    {
        const float t = v1[i] - v2[i];
        v1[i] += v2[i];
        v2[i] = t;
    }
}

AVFloatDSPContext* avpriv_float_dsp_alloc(int bit_exact)
{
    (void)bit_exact;
    AVFloatDSPContext* dsp = calloc(1, sizeof(*dsp));
    if (dsp == NULL)
        return NULL;
    dsp->vector_fmul_add = vector_fmul_add;
    dsp->vector_fmul_reverse = vector_fmul_reverse;
    dsp->butterflies_float = butterflies_float;
    return dsp;
}

/* Sine windows */

static float sine_32[32], sine_64[64], sine_128[128], sine_256[256], sine_512[512], sine_1024[1024], sine_2048[2048], sine_4096[4096], sine_8192[8192];

float* const ff_sine_windows[14] = { NULL, NULL, NULL, NULL, NULL, sine_32, sine_64, sine_128, sine_256, sine_512, sine_1024, sine_2048, sine_4096, sine_8192 };

static pthread_once_t sine_once = PTHREAD_ONCE_INIT;

static void fill_sine_windows(void)
{
    for (int index = 5; index < 14; index++)
    {
        const int n = 1 << index;
        for (int i = 0; i < n; i++)
            ff_sine_windows[index][i] = sinf((i + 0.5) * (M_PI / (2.0 * n)));
    }
}

void ff_init_ff_sine_windows(int index)
{
    (void)index;
    pthread_once(&sine_once, fill_sine_windows);
}

/* Inverse MDCT
 *
 * For n coefficients X, out[i] = -scale * sum over k of X[k] * cos(pi / n * (i + 1/2 + n/2) * (k + 1/2)), for i
 * from 0 to 2n - 1. Its middle half is a DCT-IV of X, worked out with an n/2 point complex FFT. */

struct AVTXContext {
    int n;
    float scale;
    float* pre;  /* n/2 complex pre-twiddles: exp(-i pi (4m + 1) / 4n) */
    float* post; /* n/2 complex post-twiddles: exp(-i pi m / n) */
    float* fft;  /* n/2 complex roots: exp(-2 pi i k / (n/2)) */
    float* work; /* n/2 complex values */
    float* dct;  /* n values */
};

static void fft_forward(const AVTXContext* s, float* z)
{
    const int m = s->n / 2;
    for (int i = 1, j = 0; i < m; i++)
    {
        int bit = m >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            const float re = z[2 * i], im = z[2 * i + 1];
            z[2 * i] = z[2 * j];
            z[2 * i + 1] = z[2 * j + 1];
            z[2 * j] = re;
            z[2 * j + 1] = im;
        }
    }
    for (int len = 2; len <= m; len <<= 1)
    {
        const int step = m / len;
        for (int start = 0; start < m; start += len)
        {
            for (int k = 0; k < len / 2; k++)
            {
                const float wr = s->fft[2 * k * step], wi = s->fft[2 * k * step + 1];
                float* a = z + 2 * (start + k);
                float* b = z + 2 * (start + k + len / 2);
                const float tr = b[0] * wr - b[1] * wi;
                const float ti = b[0] * wi + b[1] * wr;
                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
        }
    }
}

static void imdct_full(AVTXContext* s, void* out_, void* in_, ptrdiff_t stride)
{
    const int n = s->n, m = n / 2;
    const float* in = in_;
    float* out = out_;
    const ptrdiff_t step = stride / (ptrdiff_t)sizeof(float);
    float* z = s->work;
    for (int i = 0; i < m; i++)
    {
        const float re = in[2 * i * step], im = in[(n - 1 - 2 * i) * step];
        const float wr = s->pre[2 * i], wi = s->pre[2 * i + 1];
        z[2 * i] = re * wr - im * wi;
        z[2 * i + 1] = re * wi + im * wr;
    }
    fft_forward(s, z);
    for (int i = 0; i < m; i++)
    {
        const float wr = s->post[2 * i], wi = s->post[2 * i + 1];
        s->dct[2 * i] = z[2 * i] * wr - z[2 * i + 1] * wi;
        s->dct[n - 1 - 2 * i] = -(z[2 * i] * wi + z[2 * i + 1] * wr);
    }
    const float g = -s->scale;
    const float* u = s->dct;
    for (int i = 0; i < m; i++)
        out[i] = g * u[i + m];
    for (int i = m; i < 3 * m; i++)
        out[i] = -g * u[3 * m - 1 - i];
    for (int i = 3 * m; i < 2 * n; i++)
        out[i] = -g * u[i - 3 * m];
}

void av_tx_uninit(AVTXContext** ctx)
{
    if (ctx == NULL || *ctx == NULL)
        return;
    AVTXContext* s = *ctx;
    free(s->pre);
    free(s->post);
    free(s->fft);
    free(s->work);
    free(s->dct);
    free(s);
    *ctx = NULL;
}

int av_tx_init(AVTXContext** ctx, av_tx_fn* tx, enum AVTXType type, int inv, int len, const void* scale, uint64_t flags)
{
    *ctx = NULL;
    if (type != AV_TX_FLOAT_MDCT || !inv || !(flags & AV_TX_FULL_IMDCT) || len < 16 || (len & (len - 1)) != 0)
        return AVERROR(EINVAL);
    AVTXContext* s = calloc(1, sizeof(*s));
    if (s == NULL)
        return AVERROR(ENOMEM);
    const int m = len / 2;
    s->n = len;
    s->scale = *(const float*)scale;
    s->pre = malloc(sizeof(float) * 2 * m);
    s->post = malloc(sizeof(float) * 2 * m);
    s->fft = malloc(sizeof(float) * 2 * m);
    s->work = malloc(sizeof(float) * 2 * m);
    s->dct = malloc(sizeof(float) * len);
    *ctx = s;
    if (!s->pre || !s->post || !s->fft || !s->work || !s->dct)
    {
        av_tx_uninit(ctx);
        return AVERROR(ENOMEM);
    }
    for (int i = 0; i < m; i++)
    {
        const double a = -M_PI * (4 * i + 1) / (4.0 * len);
        const double b = -M_PI * i / len;
        const double c = -2 * M_PI * i / m;
        s->pre[2 * i] = (float)cos(a);
        s->pre[2 * i + 1] = (float)sin(a);
        s->post[2 * i] = (float)cos(b);
        s->post[2 * i + 1] = (float)sin(b);
        s->fft[2 * i] = (float)cos(c);
        s->fft[2 * i + 1] = (float)sin(c);
    }
    *tx = imdct_full;
    return 0;
}

/* Prefix codes */

static unsigned read_value(const void* table, int index, int wrap, int size)
{
    const uint8_t* p = (const uint8_t*)table + (ptrdiff_t)index * wrap;
    switch (size)
    {
    case 1: return *p;
    case 2: { uint16_t v; memcpy(&v, p, 2); return v; }
    default: { uint32_t v; memcpy(&v, p, 4); return v; }
    }
}

static int vlc_start(VLC* vlc, int nb_codes)
{
    vlc->table_size = 2 * (nb_codes > 1 ? nb_codes : 2);
    vlc->table = malloc(sizeof(VLCElem) * vlc->table_size);
    if (vlc->table == NULL)
        return AVERROR(ENOMEM);
    for (int i = 0; i < vlc->table_size; i++)
        vlc->table[i] = (VLCElem) { 0, -1 };
    return 0;
}

/* Adds the `len` bit `code` (right-aligned) for `sym`; `nodes` counts the nodes in use. */
static int vlc_add(VLC* vlc, int* nodes, uint32_t code, int len, int sym)
{
    if (len <= 0 || len > 32)
        return AVERROR_INVALIDDATA;
    int node = 0;
    for (int i = len - 1; i >= 0; i--)
    {
        VLCElem* e = &vlc->table[2 * node + ((code >> i) & 1)];
        if (i == 0)
        {
            if (e->len != -1)
                return AVERROR_INVALIDDATA;
            *e = (VLCElem) { (int16_t)sym, 1 };
            return 0;
        }
        if (e->len == 1)
            return AVERROR_INVALIDDATA;
        if (e->len == -1)
        {
            if (2 * (*nodes + 1) > vlc->table_size)
                return AVERROR_INVALIDDATA;
            *e = (VLCElem) { (int16_t)(*nodes)++, 0 };
        }
        node = e->sym;
    }
    return 0;
}

int ff_vlc_init_sparse(VLC* vlc, int nb_codes, const void* bits, int bits_wrap, int bits_size, const void* codes, int codes_wrap, int codes_size)
{
    int ret = vlc_start(vlc, nb_codes);
    int nodes = 1;
    for (int i = 0; ret == 0 && i < nb_codes; i++)
    {
        const int len = (int)read_value(bits, i, bits_wrap, bits_size);
        if (len != 0)
            ret = vlc_add(vlc, &nodes, read_value(codes, i, codes_wrap, codes_size), len, i);
    }
    if (ret < 0)
        ff_vlc_free(vlc);
    return ret;
}

int ff_vlc_init_from_lengths(VLC* vlc, int nb_bits, int nb_codes, const int8_t* lens, int lens_wrap, const void* symbols, int symbols_wrap, int symbols_size, int offset, int flags, void* logctx)
{
    (void)nb_bits;
    (void)flags;
    (void)logctx;
    int ret = vlc_start(vlc, nb_codes);
    int nodes = 1;
    uint64_t code = 0; /* Left-aligned in 32 bits */
    for (int i = 0; ret == 0 && i < nb_codes; i++)
    {
        int len = lens[(ptrdiff_t)i * lens_wrap];
        if (len > 0)
        {
            const int sym = (symbols ? (int)read_value(symbols, i, symbols_wrap, symbols_size) : i) + offset;
            ret = vlc_add(vlc, &nodes, (uint32_t)(code >> (32 - len)), len, sym);
        }
        else if (len < 0)
            len = -len;
        else
            continue;
        code += UINT64_C(1) << (32 - len);
        if (code > UINT32_MAX + UINT64_C(1))
            ret = AVERROR_INVALIDDATA;
    }
    if (ret < 0)
        ff_vlc_free(vlc);
    return ret;
}

void ff_vlc_free(VLC* vlc)
{
    free(vlc->table);
    vlc->table = NULL;
    vlc->table_size = 0;
}

/* Output frames */

int ff_get_buffer(AVCodecContext* avctx, AVFrame* frame, int flags)
{
    (void)flags;
    const int channels = avctx->ch_layout.nb_channels;
    if (frame->nb_samples <= 0 || channels <= 0 || channels > 2)
        return AVERROR(EINVAL);
    if (frame->nb_samples > frame->capacity)
    {
        for (int ch = 0; ch < 2; ch++)
        {
            float* p = realloc(frame->planes[ch], sizeof(float) * frame->nb_samples);
            if (p == NULL)
                return AVERROR(ENOMEM);
            frame->planes[ch] = p;
        }
        frame->capacity = frame->nb_samples;
    }
    frame->extended_data = (uint8_t**)frame->planes;
    return 0;
}
