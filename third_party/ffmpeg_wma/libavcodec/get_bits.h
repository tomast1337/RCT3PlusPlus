/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVCODEC_GET_BITS_H
#define RCT3_AVCODEC_GET_BITS_H

#include <stdint.h>

/**
 * Reads bits most significant first. Like FFmpeg's checked reader, the position stops 8 bits past the end;
 * bits past the end of the buffer read as zero.
 */
/** The most bits get_bits reads at once in FFmpeg. */
#define MIN_CACHE_BITS 25

typedef struct GetBitContext {
    const uint8_t* buffer;
    int index;
    int size_in_bits;
} GetBitContext;

static inline int init_get_bits(GetBitContext* s, const uint8_t* buffer, int bit_size)
{
    if (bit_size < 0 || buffer == 0)
        bit_size = 0;
    s->buffer = buffer;
    s->index = 0;
    s->size_in_bits = bit_size;
    return bit_size == 0 && buffer == 0 ? -1 : 0;
}

static inline unsigned rct3_peek_bits(const GetBitContext* s, int n)
{
    unsigned value = 0;
    const int bytes = (s->size_in_bits + 7) >> 3;
    for (int i = 0; i < n; i++)
    {
        const int at = s->index + i;
        const int byte = at >> 3;
        const unsigned bit = byte < bytes ? (s->buffer[byte] >> (7 - (at & 7))) & 1u : 0u;
        value = (value << 1) | bit;
    }
    return value;
}

static inline void skip_bits(GetBitContext* s, int n)
{
    const int end = s->size_in_bits + 8;
    s->index = s->index + n > end ? end : s->index + n;
}

static inline unsigned int get_bits_long(GetBitContext* s, int n)
{
    const unsigned value = rct3_peek_bits(s, n);
    skip_bits(s, n);
    return value;
}

static inline unsigned int get_bits(GetBitContext* s, int n)
{
    return get_bits_long(s, n);
}

static inline unsigned int get_bits1(GetBitContext* s)
{
    return get_bits_long(s, 1);
}

static inline int get_bits_count(const GetBitContext* s)
{
    return s->index;
}

static inline int get_bits_left(const GetBitContext* s)
{
    return s->size_in_bits - s->index;
}

static inline void align_get_bits(GetBitContext* s)
{
    const int n = -s->index & 7;
    if (n)
        skip_bits(s, n);
}

/**
 * A prefix code as a binary tree: entries 2k and 2k + 1 are node k's children for a 0 and a 1 bit. An
 * entry with len 1 is a leaf holding a symbol, len 0 points at node sym, len -1 is no code.
 */
typedef struct VLCElem {
    int16_t sym;
    int16_t len;
} VLCElem;

typedef struct VLC {
    VLCElem* table;
    int table_size;
} VLC;

/** Reads one code; -1 for bits that start no code. `bits` and `max_depth` are FFmpeg's and unused here. */
static inline int get_vlc2(GetBitContext* s, const VLCElem* table, int bits, int max_depth)
{
    (void)bits;
    (void)max_depth;
    int node = 0;
    for (int depth = 0; depth < 32; depth++)
    {
        const VLCElem e = table[2 * node + (int)get_bits1(s)];
        if (e.len == 1)
            return e.sym;
        if (e.len < 0)
            return -1;
        node = e.sym;
    }
    return -1;
}

/** Codes `codes` of lengths `bits` (each read from `size` bytes, `wrap` bytes apart) for symbols 0 to n - 1. */
int ff_vlc_init_sparse(VLC* vlc, int nb_codes, const void* bits, int bits_wrap, int bits_size, const void* codes, int codes_wrap, int codes_size);
#define vlc_init(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size, codes, codes_wrap, codes_size, flags) \
    ff_vlc_init_sparse((vlc), (nb_codes), (bits), (bits_wrap), (bits_size), (codes), (codes_wrap), (codes_size))

/** Canonical codes, given in order of the code, with their lengths and symbols (plus `offset`). */
int ff_vlc_init_from_lengths(VLC* vlc, int nb_bits, int nb_codes, const int8_t* lens, int lens_wrap, const void* symbols, int symbols_wrap, int symbols_size, int offset, int flags, void* logctx);

void ff_vlc_free(VLC* vlc);

#endif
