// SPDX-License-Identifier: GPL-3.0-or-later
//
// The single translation unit that compiles the miniaudio implementation. Backends are loaded at run time
// (dlopen on Linux), nothing audio-related is linked at build time.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio_config.h"
