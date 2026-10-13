/*
 * Part of the WMA decoder cut from FFmpeg for rct3pp: a small stand-in for the
 * FFmpeg header of the same name, written for rct3pp, holding only what the
 * decoder files use. See README.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef RCT3_AVUTIL_FFMATH_H
#define RCT3_AVUTIL_FFMATH_H

#include <math.h>

/** 10 to the power x. */
static inline double ff_exp10(double x)
{
    return exp2(3.32192809488736234787 * x);
}

#endif
