// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font_sets.hpp"

#include "ui/language.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui
{

    std::string font_overlay(std::string_view language, int width)
    {
        const auto info = find_language(language);
        const std::string_view stem = info && info->cjk ? info->name : std::string_view("Resolution");
        return std::format("gui/{}/{}.common.ovl", width, stem);
    }

    std::optional<FontRole> set_font_role(std::string_view name)
    {
        const auto it = std::ranges::find(kSetFonts, name, &std::pair<FontRole, std::string_view>::second);
        return it != kSetFonts.end() ? std::optional(it->first) : std::nullopt;
    }

} // namespace rct3::ui
