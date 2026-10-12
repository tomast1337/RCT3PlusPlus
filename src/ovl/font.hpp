// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fonts: a face description plus a glyph table that maps character codes to rectangles of a texture.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Bytes of one glyph row in the file: nine 32-bit words.
    inline constexpr std::size_t kFontGlyphSize = 36;

    struct FontGlyph
    {
        std::uint32_t code = 0; ///< Character code, a UTF-16 code unit
        /// Horizontal step to the next glyph, as a fraction of the font's cell size. Multiply by the text's
        /// horizontal scale (pixels per cell) to get the step in pixels.
        float advance = 0;
        /// Extra space added after the glyph on top of the advance, also a fraction of the cell size. Usually
        /// 0 or one sixteenth, occasionally negative. It is not a pair kerning value.
        float lead = 0;
        /// Rectangle of the glyph in the font texture page, as {u0, v0, u1, v1}, normalised. Pages are 256
        /// texels square in shipped files, so the rectangle is a whole number of texels. A rectangle with no
        /// area (for example {0, 1, 0, 1}) is a glyph that draws nothing but still has an advance. The page
        /// rows are stored bottom up, so v grows towards the top of the glyph on screen.
        std::array<float, 4> texcoords { };
        /// Placement of the rectangle, in texels of the page: `shift[0]` is the offset of its left edge from
        /// the pen position, `shift[1]` the offset of its bottom edge above the baseline (negative for
        /// descenders).
        std::array<float, 2> shift { };
        /// Index of the texture page (frame of the font's texture) that holds the rectangle. Read from the
        /// upper half of the row's third word; the lower half is leftover data.
        std::uint16_t page = 0;
    };

    struct Font
    {
        std::string_view face;    ///< Name of the typeface the glyphs were made from, a string in the file
        std::string_view texture; ///< Name of the texture item holding the glyphs, without the tag. Empty if none.
        /// Three fractions of the font's cell size N (the cell size in pixels is 1 / metrics[0]): the first is
        /// 1/N, the second the line height and the third the distance from the bottom of a line to the
        /// baseline, as far as the layout code shows.
        std::array<float, 3> metrics { };
        std::uint32_t weight = 0; ///< 400 normal or 700 bold
        /// Code of the first glyph for fonts without a code table (glyph i has code first_char + i), 0 otherwise.
        std::uint32_t first_char = 0;
        /// 1: the glyph data in the file is used as is. 0: the face is meant to be rendered by the system when
        /// it is installed and the file data is only the fallback.
        std::uint32_t flag = 0;
        bool has_code_table = false; ///< True when each glyph row has its own code, false for one range
        /// Glyph rows in file order. With a code table the last row is the terminator (code 0) when present.
        std::vector<FontGlyph> glyphs;
    };

    struct FontItem
    {
        std::string_view name;    ///< Symbol name without the tag
        std::optional<Font> font; ///< Nothing when any part of the structure is unreadable
    };

    /// The glyph for a UTF-16 code unit, or nullptr when the font has none (text layout draws nothing and does
    /// not advance for such a unit). With a code table the table is searched from the start, the first match
    /// wins, code 0 and anything above 0xFFFF never match and a row with code 0 ends the search. Without one,
    /// the glyphs cover consecutive codes starting at `first_char`. There is no fallback glyph and surrogate
    /// halves are looked up like any other unit.
    [[nodiscard]] const FontGlyph* find_glyph(const Font& font, std::uint32_t code) noexcept;

    /// Fonts of one part of an overlay: pointer symbols tagged "fct". Views in the result point into the
    /// overlay's bytes.
    [[nodiscard]] std::vector<FontItem> read_fonts(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
