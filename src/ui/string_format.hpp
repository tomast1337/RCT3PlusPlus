// SPDX-License-Identifier: GPL-3.0-or-later
//
// Filling in a translated string. A string names the values it shows in braces, "{count} guests in
// {park}", so a translation can put them in any order; "{{" and "}}" stand for the braces themselves. A
// placeholder with no value given, or a brace that opens nothing, is left as written so the gap shows.
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace rct3::ui
{

    struct FormatArg
    {
        std::u16string_view name;
        std::u16string value;
    };

    [[nodiscard]] std::u16string format_string(std::u16string_view pattern, std::span<const FormatArg> args);

} // namespace rct3::ui
