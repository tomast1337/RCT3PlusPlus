// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font.hpp"

#include <algorithm>

namespace rct3::ui
{

    Font Font::from(const ovl::Font& font, std::span<const Page> pages)
    {
        Font out;
        if (!(font.metrics[0] > 0))
            return out;
        out.cell_ = 1 / font.metrics[0];
        out.line_height_ = font.metrics[1] * out.cell_;
        out.descent_ = font.metrics[2] * out.cell_;

        for (std::size_t i = 0; i < font.glyphs.size(); ++i)
        {
            const ovl::FontGlyph& g = font.glyphs[i];
            std::uint32_t code = 0;
            if (font.has_code_table)
            {
                if (g.code == 0) // The terminator ends the table
                    break;
                code = g.code;
            }
            else
            {
                code = font.first_char + static_cast<std::uint32_t>(i);
            }
            if (code == 0 || code > 0xFFFF)
                continue;

            Glyph glyph;
            glyph.advance = (g.advance + g.lead) * out.cell_;
            const auto& [u0, v0, u1, v1] = g.texcoords;
            if (g.page < pages.size() && u1 > u0 && v1 > v0)
            {
                const Page& page = pages[g.page];
                const float w = (u1 - u0) * page.width;
                const float h = (v1 - v0) * page.height;
                const float bottom = -g.shift[1];
                glyph.box = { g.shift[0], bottom - h, g.shift[0] + w, bottom };
                glyph.uv = { u0, v1, u1, v0 }; // v grows towards the top of the glyph
                glyph.page = page.texture;
            }
            out.index_.emplace_back(static_cast<char16_t>(code), static_cast<std::uint32_t>(out.glyphs_.size()));
            out.glyphs_.push_back(glyph);
        }

        // The first row with a code wins, as in the file's own lookup.
        std::ranges::stable_sort(out.index_, { }, &std::pair<char16_t, std::uint32_t>::first);
        const auto [first, last] = std::ranges::unique(out.index_, { }, &std::pair<char16_t, std::uint32_t>::first);
        out.index_.erase(first, last);
        return out;
    }

    const Glyph* Font::find(char16_t code) const
    {
        const auto it = std::ranges::lower_bound(index_, code, { }, &std::pair<char16_t, std::uint32_t>::first);
        if (it == index_.end() || it->first != code)
            return nullptr;
        return &glyphs_[it->second];
    }

} // namespace rct3::ui
