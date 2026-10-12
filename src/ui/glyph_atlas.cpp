// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/glyph_atlas.hpp"

#include <algorithm>
#include <cstddef>

namespace rct3::ui
{

    namespace
    {
        constexpr std::uint32_t kGap = 1;
        constexpr std::uint32_t kNoPage = static_cast<std::uint32_t>(-1);
    } // namespace

    std::vector<AtlasSlot> pack_atlas(std::span<const AtlasSize> sizes, std::uint32_t page_side)
    {
        std::vector<AtlasSlot> slots;
        slots.reserve(sizes.size());
        std::uint32_t page = 0;
        std::uint32_t x = kGap;
        std::uint32_t y = kGap;
        std::uint32_t row = 0; // Height of the current row
        for (const AtlasSize& s : sizes)
        {
            if (s.width == 0 || s.height == 0)
            {
                slots.push_back({ page, 0, 0 });
                continue;
            }
            if (s.width + 2 * kGap > page_side || s.height + 2 * kGap > page_side)
            {
                slots.push_back({ kNoPage, 0, 0 });
                continue;
            }
            if (x + s.width + kGap > page_side)
            {
                x = kGap;
                y += row + kGap;
                row = 0;
            }
            if (y + s.height + kGap > page_side)
            {
                ++page;
                x = kGap;
                y = kGap;
                row = 0;
            }
            slots.push_back({ page, x, y });
            x += s.width + kGap;
            row = std::max(row, s.height);
        }
        return slots;
    }

    std::uint32_t atlas_page_count(std::span<const AtlasSlot> slots)
    {
        std::uint32_t count = 0;
        for (const AtlasSlot& s : slots)
        {
            if (s.page != kNoPage)
                count = std::max(count, s.page + 1);
        }
        return count;
    }

    std::vector<ovl::RgbaImage> atlas_pages(std::span<const CoverageBitmap> bitmaps, std::span<const AtlasSlot> slots, std::uint32_t page_side)
    {
        std::vector<ovl::RgbaImage> pages(atlas_page_count(slots));
        for (ovl::RgbaImage& p : pages)
        {
            p.width = page_side;
            p.height = page_side;
            p.pixels.assign(std::size_t { page_side } * page_side * 4, 0);
            for (std::size_t i = 0; i < p.pixels.size(); i += 4)
                std::fill_n(p.pixels.begin() + static_cast<std::ptrdiff_t>(i), 3, std::uint8_t { 255 });
        }
        const std::size_t n = std::min(bitmaps.size(), slots.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const CoverageBitmap& b = bitmaps[i];
            const AtlasSlot& s = slots[i];
            if (s.page == kNoPage || b.coverage.size() < std::size_t { b.width } * b.height)
                continue;
            ovl::RgbaImage& page = pages[s.page];
            for (std::uint32_t row = 0; row < b.height && s.y + row < page_side; ++row)
            {
                for (std::uint32_t col = 0; col < b.width && s.x + col < page_side; ++col)
                {
                    const std::size_t to = ((std::size_t { s.y } + row) * page_side + s.x + col) * 4 + 3;
                    page.pixels[to] = b.coverage[std::size_t { row } * b.width + col];
                }
            }
        }
        return pages;
    }

} // namespace rct3::ui
