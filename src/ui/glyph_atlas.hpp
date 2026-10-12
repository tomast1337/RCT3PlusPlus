// SPDX-License-Identifier: GPL-3.0-or-later
//
// Packing glyph bitmaps into square page images. Shelves: glyphs go left to right in rows as tall as the
// tallest glyph in them, a new row starts when one is full and a new page when the rows reach the bottom.
// Every glyph keeps a one-pixel gap to its neighbours so filtering never reads the next glyph.
#pragma once

#include "ovl/texture_image.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rct3::ui
{

    struct AtlasSlot
    {
        std::uint32_t page = 0;
        std::uint32_t x = 0; ///< Top-left corner, rows counted from the top
        std::uint32_t y = 0;
    };

    struct AtlasSize
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /// A slot for each size, in order, on pages `page_side` pixels square. Sizes with a zero side take no
    /// space (slot at 0, 0 on the current page). Sizes that cannot fit an empty page get page index
    /// `std::uint32_t(-1)`.
    [[nodiscard]] std::vector<AtlasSlot> pack_atlas(std::span<const AtlasSize> sizes, std::uint32_t page_side);

    /// The number of pages `slots` use.
    [[nodiscard]] std::uint32_t atlas_page_count(std::span<const AtlasSlot> slots);

    /// A glyph's coverage, one byte per pixel, rows from the top.
    struct CoverageBitmap
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> coverage;
    };

    /// White pages with coverage as alpha, so the text colour tints them. Bitmaps whose slot has no page are
    /// left out.
    [[nodiscard]] std::vector<ovl::RgbaImage> atlas_pages(std::span<const CoverageBitmap> bitmaps, std::span<const AtlasSlot> slots, std::uint32_t page_side);

} // namespace rct3::ui
