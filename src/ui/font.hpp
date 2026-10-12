// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game font made ready to draw: each glyph as a box relative to the pen on the baseline, the part of a
// page texture it shows, and how far it moves the pen. Built once from the font item and its uploaded
// pages; text layout then works in logical units, one unit per texel of the page at the font's own size.
//
// A font can take another as its fallback: codes it lacks are drawn with the other font's glyphs. Glyph
// boxes hang off the baseline, so borrowed glyphs share this font's baseline; the line height and ascent stay
// this font's. Size the fallback to match before merging it (see fallback_font.hpp).
#pragma once

#include "ovl/font.hpp"
#include "ui/draw_list.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct Glyph
    {
        Rect box; ///< Relative to the pen on the baseline, y down. Empty for glyphs that draw nothing.
        Rect uv;  ///< In the page texture, 0..1, top row first
        TextureId page = 0;
        float advance = 0; ///< Pen movement, spacing included
    };

    struct CodedGlyph
    {
        char16_t code = 0;
        Glyph glyph;
    };

    /// Fonts made from this face take the room for the descent twice in each line.
    inline constexpr std::string_view kRoomyFace = "SimSun";

    class Font
    {
    public:
        /// A page texture of the font, in the order the font's page indices count.
        struct Page
        {
            TextureId texture = 0;
            float width = 0;
            float height = 0;
        };

        Font() = default;
        /// Glyphs on pages that are missing still advance the pen but draw nothing.
        [[nodiscard]] static Font from(const ovl::Font& font, std::span<const Page> pages);
        /// The same, for a font item whose face is no longer at hand: `face` names it.
        [[nodiscard]] static Font from(const ovl::Font& font, std::span<const Page> pages, std::string_view face);
        /// A font from ready glyphs; the first glyph with a code wins. `line_height` is also the cell.
        [[nodiscard]] static Font from_glyphs(std::span<const CodedGlyph> glyphs, float line_height, float descent);

        /// This font with the glyphs of `fallback` added for every code it does not have.
        [[nodiscard]] Font with_fallback(const Font& fallback) const;

        /// The glyph for a UTF-16 code unit, with the lookup rules of ovl::find_glyph. Nothing for a unit the
        /// font does not have; such units draw nothing and do not advance.
        [[nodiscard]] const Glyph* find(char16_t code) const;

        [[nodiscard]] float cell() const
        {
            return cell_;
        }
        /// Distance between the tops of consecutive lines: the font's line height and its descent, the
        /// descent twice for kRoomyFace.
        [[nodiscard]] float line_height() const
        {
            return line_height_;
        }
        /// Distance from the baseline up to the top of a line.
        [[nodiscard]] float ascent() const
        {
            return line_height_ - descent_;
        }

    private:
        void sort_index();

        float cell_ = 0;
        float line_height_ = 0;
        float descent_ = 0;
        std::vector<Glyph> glyphs_;
        std::vector<std::pair<char16_t, std::uint32_t>> index_; ///< Code to glyph, sorted by code
    };

} // namespace rct3::ui
