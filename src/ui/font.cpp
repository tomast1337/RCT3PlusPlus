// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font.hpp"

#include <algorithm>

namespace rct3::ui
{

    Font Font::from(const ovl::Font& font, std::span<const Page> pages)
    {
        return from(font, pages, font.face);
    }

    Font Font::from(const ovl::Font& font, std::span<const Page> pages, std::string_view face)
    {
        Font out;
        if (!(font.metrics[0] > 0))
            return out;
        out.cell_ = 1 / font.metrics[0];
        // A line is the line height and the descent below it; SimSun fonts leave room for the descent twice,
        // the extra above the baseline.
        const float descents = face == kRoomyFace ? 2.0f : 1.0f;
        out.line_height_ = (font.metrics[1] + descents * font.metrics[2]) * out.cell_;
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

        out.sort_index(); // The first row with a code wins, as in the file's own lookup
        return out;
    }

    Font Font::from_glyphs(std::span<const CodedGlyph> glyphs, float line_height, float descent)
    {
        Font out;
        out.cell_ = line_height;
        out.line_height_ = line_height;
        out.descent_ = descent;
        for (const CodedGlyph& g : glyphs)
        {
            out.index_.emplace_back(g.code, static_cast<std::uint32_t>(out.glyphs_.size()));
            out.glyphs_.push_back(g.glyph);
        }
        out.sort_index();
        return out;
    }

    Font Font::with_fallback(const Font& fallback) const
    {
        Font out = *this;
        for (const auto& [code, index] : fallback.index_)
        {
            if (find(code) != nullptr)
                continue;
            out.index_.emplace_back(code, static_cast<std::uint32_t>(out.glyphs_.size()));
            out.glyphs_.push_back(fallback.glyphs_[index]);
        }
        out.sort_index();
        return out;
    }

    void Font::sort_index()
    {
        // Stable, so among rows with the same code the earliest stays and unique() keeps it.
        std::ranges::stable_sort(index_, { }, &std::pair<char16_t, std::uint32_t>::first);
        const auto [first, last] = std::ranges::unique(index_, { }, &std::pair<char16_t, std::uint32_t>::first);
        index_.erase(first, last);
    }

    const Glyph* Font::find(char16_t code) const
    {
        const auto it = std::ranges::lower_bound(index_, code, { }, &std::pair<char16_t, std::uint32_t>::first);
        if (it == index_.end() || it->first != code)
            return nullptr;
        return &glyphs_[it->second];
    }

} // namespace rct3::ui
