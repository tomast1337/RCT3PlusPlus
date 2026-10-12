// SPDX-License-Identifier: GPL-3.0-or-later
//
// Include this instead of miniaudio.h. miniaudio's structs change layout with these switches, so every
// translation unit that sees the header must see the same ones. Only raw PCM playback is needed here:
// file decoders, encoders, the resource manager and generators are switched off.
#pragma once

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER

#include "miniaudio/miniaudio.h"
