// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's text comes in three sizes of font set, made for screens 800, 1024 and 1280 pixels wide;
// the window chrome is the same in all three. The set follows the width of the interface: up to 800 units
// takes the smallest, up to 1024 the middle one and anything wider the largest. Each set holds the same
// fonts by name, sized for its width, plus one with the glyphs of every language's own name, for a
// language list.
//
// Languages that bring their own fonts (LanguageInfo::cjk) have a set of their own for each width, in
// place of the shared one.
#pragma once

#include "ui/font_set.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rct3::ui
{

    /// The widths there are sets for, smallest first.
    inline constexpr std::array<int, 3> kFontSetWidths = { 800, 1024, 1280 };

    /// The width of the set for an interface `view_width` units wide.
    [[nodiscard]] constexpr int font_set_width(float view_width)
    {
        for (const int w : kFontSetWidths)
        {
            if (view_width <= static_cast<float>(w))
                return w;
        }
        return kFontSetWidths.back();
    }

    /// The overlay with the fonts for `language` at set `width`, relative to the install root.
    [[nodiscard]] std::string font_overlay(std::string_view language, int width);

    /// The fonts of a set by role, as they are named in its overlays.
    inline constexpr std::array<std::pair<FontRole, std::string_view>, kFontRoleCount> kSetFonts = { {
        { FontRole::Tiny, "GUIFontScaledTiny" },
        { FontRole::Small, "GUIFontScaledSmall" },
        { FontRole::Medium, "GUIFontScaledMedium" },
        { FontRole::Large, "GUIFontScaledLarge" },
        { FontRole::AllScripts, "GUIFontScaledAllLanguages" },
    } };

    /// The role a set's font named `name` fills, if any.
    [[nodiscard]] std::optional<FontRole> set_font_role(std::string_view name);

} // namespace rct3::ui
