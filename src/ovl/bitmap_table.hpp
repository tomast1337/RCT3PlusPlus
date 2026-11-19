// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bitmap tables: the pixel data of the textures in a file, stored once as a list of bitmaps with their
// mip chains. A table has no symbol name, it is attached to an anonymous loader row.
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::ovl
{

    /// How the pixels of a bitmap format are stored.
    enum class BitmapLayout
    {
        Pixels32, ///< 4 bytes per pixel: blue, green, red, alpha
        Blocks8,  ///< 4x4 pixel blocks of 8 bytes: DXT1 compression
        Blocks16, ///< 4x4 pixel blocks of 16 bytes: DXT3 compression, explicit 4-bit alpha
    };

    /// Layout of a stored format value: 2 is 32 bits per pixel (B8G8R8A8), 18 is the 8-byte block format (DXT1) and 19 the 16-byte
    /// one (DXT3), the two blocks formats being 4x4 pixels. Nothing for any other value, which does not occur in shipped files.
    [[nodiscard]] std::optional<BitmapLayout> bitmap_layout(u32 format) noexcept;

    /// Bytes of all `levels` mip levels of a bitmap, stored from the largest level down with each level
    /// halving both sides down to 1. Nothing for an unknown format.
    [[nodiscard]] std::optional<u64> bitmap_data_size(u32 format, u32 width, u32 height, u32 levels) noexcept;

    struct Bitmap
    {
        u32 format = 0;
        u32 width = 0;                   ///< Power of two, at most 2048
        u32 height = 0;                  ///< Power of two, at most 2048
        u32 levels = 0;                  ///< Mip levels, 1 to 11
        std::span<const std::byte> data; ///< All levels in order, `bitmap_data_size` bytes
    };

    struct BitmapTable
    {
        std::vector<Bitmap> bitmaps;
    };

    struct BitmapTableItem
    {
        usize row = 0;                    ///< Index of the loader row the table belongs to
        std::optional<BitmapTable> table; ///< Nothing when the row's data or chunks are unreadable
    };

    /// Bitmap tables of one part of an overlay: loader rows of type "btbl". Spans in the result point into
    /// the part's bytes.
    [[nodiscard]] std::vector<BitmapTableItem> read_bitmap_tables(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
