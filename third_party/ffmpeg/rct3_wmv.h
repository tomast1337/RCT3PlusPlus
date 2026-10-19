/*
 * FFmpeg's Windows Media Video 9 (WMV3) decoder, behind a small C interface written for rct3pp. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_WMV_H
#define RCT3_WMV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Rct3WmvDecoder Rct3WmvDecoder;

/** The stream's format, as the container gives it. */
typedef struct Rct3WmvFormat {
    int width;
    int height;
    const uint8_t* extra; /* The codec bytes after the bitmap header */
    int extra_size;
} Rct3WmvFormat;

/** A decoded picture: 8-bit Y, U and V planes, U and V at half the width and height. */
typedef struct Rct3WmvPicture {
    int width;
    int height;
    const uint8_t* planes[3];
    int strides[3];
} Rct3WmvPicture;

/** NULL for a format the decoder cannot take. */
Rct3WmvDecoder* rct3_wmv_open(const Rct3WmvFormat* format);
void rct3_wmv_close(Rct3WmvDecoder* decoder);

/**
 * Decodes one whole frame of the stream. Returns 1 with the picture in *picture (valid until the next call),
 * 0 when the frame gives none yet, or a negative number for a bad frame. A frame of size 0 gives the last
 * picture still held back.
 */
int rct3_wmv_decode(Rct3WmvDecoder* decoder, const uint8_t* frame, int size, Rct3WmvPicture* picture);

#ifdef __cplusplus
}
#endif

#endif
