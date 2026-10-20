// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/texture_pixels.hpp"

#include <algorithm>
#include <cstring>

namespace rct3::ovl
{

    TexturePixels TexturePixels::index(const Overlay& overlay)
    {
        TexturePixels out;
        for (const auto part : { maybe(&overlay.common()), overlay.unique() })
        {
            if (!part)
                continue;
            for (FlicItem& item : read_flics(overlay, *part))
            {
                if (item.flic)
                    out.flics_.emplace(item.address, std::move(*item.flic));
            }
            for (BitmapTableItem& item : read_bitmap_tables(overlay, *part))
            {
                if (item.table)
                    out.tables_.push_back({ part->loaders.entries[item.row].data, std::move(*item.table) });
            }
        }
        std::ranges::sort(out.tables_, { }, &Table::address);
        return out;
    }

    std::size_t TexturePixels::frame_count(const Texture& texture) const
    {
        if (!texture.node)
            return 0;
        const auto flic = flics_.find(texture.node->address);
        if (flic == flics_.end())
            return 0;
        return flic->second.image ? 1 : flic->second.frames.size();
    }

    std::optional<RgbaImage> TexturePixels::frame(const Texture& texture, std::size_t index) const
    {
        if (!texture.node)
            return std::nullopt;
        const auto flic = flics_.find(texture.node->address);
        if (flic == flics_.end())
            return std::nullopt;

        if (flic->second.image)
        {
            if (index != 0)
                return std::nullopt;
            auto levels = decode_flic_image(flic->second);
            if (levels.empty())
                return std::nullopt;
            return std::move(levels.front());
        }
        if (index >= flic->second.frames.size())
            return std::nullopt;

        // The nearest table before the flic.
        const auto after = std::ranges::upper_bound(tables_, flic->first, { }, &Table::address);
        if (after == tables_.begin())
            return std::nullopt;
        const BitmapTable& table = std::prev(after)->table;
        const std::uint32_t bitmap = flic->second.frames[index];
        if (bitmap >= table.bitmaps.size())
            return std::nullopt;
        auto levels = decode_bitmap(table.bitmaps[bitmap]);
        if (levels.empty())
            return std::nullopt;
        return std::move(levels.front());
    }

    RgbaImage flipped_vertically(RgbaImage image)
    {
        const std::size_t row = std::size_t { image.width } * 4;
        if (image.pixels.size() != row * image.height)
            return image;
        for (std::uint32_t top = 0, bottom = image.height; top + 1 < bottom; ++top)
        {
            --bottom;
            std::swap_ranges(image.pixels.begin() + static_cast<std::ptrdiff_t>(top * row), image.pixels.begin() + static_cast<std::ptrdiff_t>((top + 1) * row), image.pixels.begin() + static_cast<std::ptrdiff_t>(bottom * row));
        }
        return image;
    }

} // namespace rct3::ovl
