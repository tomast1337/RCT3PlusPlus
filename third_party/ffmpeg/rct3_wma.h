/*
 * FFmpeg's Windows Media Audio decoder, behind a small C interface written for rct3pp. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_WMA_H
#define RCT3_WMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Rct3WmaDecoder Rct3WmaDecoder;

/** The stream's format, as the container gives it. */
typedef struct Rct3WmaFormat {
    int version;     /* 1 or 2 */
    int sample_rate; /* Up to 50000 */
    int channels;    /* 1 or 2 */
    int bit_rate;    /* Bits per second */
    int block_align; /* Bytes in each packet */
    const uint8_t* extra;
    int extra_size;
} Rct3WmaFormat;

/** NULL for a format the decoder cannot take. */
Rct3WmaDecoder* rct3_wma_open(const Rct3WmaFormat* format);
void rct3_wma_close(Rct3WmaDecoder* decoder);

/**
 * Decodes one packet of block_align bytes. Returns the samples for each channel, put in *planes (valid
 * until the next call), 0 when the packet only carries data on, or a negative number for a bad packet.
 * A packet of size 0 gives the last samples still held back.
 */
int rct3_wma_decode(Rct3WmaDecoder* decoder, const uint8_t* packet, int size, const float* const** planes);

/** Samples at the start of the decoded stream that come before the sound: the decoder's delay. */
int rct3_wma_delay(const Rct3WmaDecoder* decoder);

/** Forgets the data carried between packets, to start again elsewhere in the stream. */
void rct3_wma_reset(Rct3WmaDecoder* decoder);

#ifdef __cplusplus
}
#endif

#endif
