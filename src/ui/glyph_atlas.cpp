// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/glyph_atlas.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cstddef>

namespace rct3::ui
{

    namespace
    {
        constexpr u32 kGap = 1;
        constexpr u32 kNoPage = static_cast<u32>(-1);
    } // namespace

    std::vector<AtlasSlot> pack_atlas(std::span<const AtlasSize> sizes, u32 page_side)
    {
        std::vector<AtlasSlot> slots;
        slots.reserve(sizes.size());
        u32 page = 0;
        u32 x = kGap;
        u32 y = kGap;
        u32 row = 0; // Height of the current row
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

    u32 atlas_page_count(std::span<const AtlasSlot> slots)
    {
        u32 count = 0;
        for (const AtlasSlot& s : slots)
        {
            if (s.page != kNoPage)
                count = std::max(count, s.page + 1);
        }
        return count;
    }

    std::vector<ovl::texture::RgbaImage> atlas_pages(std::span<const CoverageBitmap> bitmaps, std::span<const AtlasSlot> slots, u32 page_side)
    {
        std::vector<ovl::texture::RgbaImage> pages(atlas_page_count(slots));
        for (ovl::texture::RgbaImage& p : pages)
        {
            p.width = page_side;
            p.height = page_side;
            p.pixels.assign(usize { page_side } * page_side * 4, 0);
            for (usize i = 0; i < p.pixels.size(); i += 4)
                std::fill_n(p.pixels.begin() + static_cast<std::ptrdiff_t>(i), 3, u8 { 255 });
        }
        const usize n = std::min(bitmaps.size(), slots.size());
        for (usize i = 0; i < n; ++i)
        {
            const CoverageBitmap& b = bitmaps[i];
            const AtlasSlot& s = slots[i];
            if (s.page == kNoPage || b.coverage.size() < usize { b.width } * b.height)
                continue;
            ovl::texture::RgbaImage& page = pages[s.page];
            for (u32 row = 0; row < b.height && s.y + row < page_side; ++row)
            {
                for (u32 col = 0; col < b.width && s.x + col < page_side; ++col)
                {
                    const usize to = ((usize { s.y } + row) * page_side + s.x + col) * 4 + 3;
                    page.pixels[to] = b.coverage[usize { row } * b.width + col];
                }
            }
        }
        return pages;
    }

} // namespace rct3::ui
