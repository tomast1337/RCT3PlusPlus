// SPDX-License-Identifier: GPL-3.0-or-later
//
// ASCII-only string helpers. Game names and identifiers are ASCII; these never apply locale rules.
#pragma once

#include <algorithm>
#include <string_view>

namespace rct3::strings
{

    [[nodiscard]] constexpr char ascii_lower(char c) noexcept
    {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    [[nodiscard]] constexpr bool equals_ignoring_case(std::string_view a, std::string_view b) noexcept
    {
        return std::ranges::equal(a, b, [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
    }

} // namespace rct3::strings
