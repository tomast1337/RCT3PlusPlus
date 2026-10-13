/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVUTIL_ATTRIBUTES_H
#define RCT3_AVUTIL_ATTRIBUTES_H

#define av_cold __attribute__((cold))
#define av_always_inline inline __attribute__((always_inline))
#define av_warn_unused_result __attribute__((warn_unused_result))
#define av_unused __attribute__((unused))

#endif
