/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVUTIL_FLOAT_DSP_H
#define RCT3_AVUTIL_FLOAT_DSP_H

/** The vector operations the decoder uses, in plain C. */
typedef struct AVFloatDSPContext {
    /** dst[i] = src0[i] * src1[i] + src2[i] */
    void (*vector_fmul_add)(float* dst, const float* src0, const float* src1, const float* src2, int len);
    /** dst[i] = src0[i] * src1[len - 1 - i] */
    void (*vector_fmul_reverse)(float* dst, const float* src0, const float* src1, int len);
    /** v1[i], v2[i] = v1[i] + v2[i], v1[i] - v2[i] */
    void (*butterflies_float)(float* v1, float* v2, int len);
} AVFloatDSPContext;

AVFloatDSPContext* avpriv_float_dsp_alloc(int bit_exact);

#endif
