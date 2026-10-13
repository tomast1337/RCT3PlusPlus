/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVCODEC_SINEWIN_H
#define RCT3_AVCODEC_SINEWIN_H

/** ff_sine_windows[n] holds 1 << n values of sin((i + 0.5) * pi / (2 << n)), once filled. */
extern float* const ff_sine_windows[14];

/** Fills ff_sine_windows[index], for index 5 to 13. */
void ff_init_ff_sine_windows(int index);

#endif
