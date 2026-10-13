// SPDX-License-Identifier: GPL-3.0-or-later
//
// The single translation unit that compiles the stb_image implementation: JPEG, PNG, BMP and TGA only, read
// from memory, never from a file name.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb/stb_image.h"
