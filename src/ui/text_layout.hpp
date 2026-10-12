// SPDX-License-Identifier: GPL-3.0-or-later
//
// Laying out and drawing UTF-16 text with a Font. Lines break at line feeds, and when wrapping, at spaces
// (dropped at the break) and after hyphens; a word wider than the box is cut between characters. Text is
// not shaped or kerned: each code unit is one glyph. Text is laid out again every time it is drawn.
#pragma once

#include "ui/draw_list.hpp"
#include "ui/font.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class Align : std::uint8_t
    {
        Start, ///< Left or top
        Centre,
        End, ///< Right or bottom
    };

    struct TextStyle
    {
        Color colour { 0, 0, 0, 255 };
        Align horizontal = Align::Start;
        Align vertical = Align::Start;
        bool wrap = false;
    };

    struct TextLine
    {
        std::u16string_view text; ///< Without the line feed or the space it broke at
        float width = 0;
    };

    /// The pen movement of `text` on one line.
    [[nodiscard]] float text_width(const Font& font, std::u16string_view text);

    /// Splits `text` into lines no wider than `max_width` where possible. A `max_width` of 0 or less breaks
    /// only at line feeds.
    [[nodiscard]] std::vector<TextLine> break_lines(const Font& font, std::u16string_view text, float max_width);

    /// Draws `text` inside `box`, aligned by `style`. Lines that do not fit spill out of the box; push a
    /// clip first to cut them.
    void draw_text(DrawList& list, const Font& font, std::u16string_view text, const Rect& box, const TextStyle& style);

} // namespace rct3::ui
