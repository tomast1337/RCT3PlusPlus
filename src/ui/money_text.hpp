// SPDX-License-Identifier: GPL-3.0-or-later
//
// Amounts of money written as the game writes them. Saves keep money in hundredths of the currency unit; the
// player picks the currency in the options. The game's strings give each currency what goes before an amount,
// what goes after it and the mark before the hundredths ("Money<n>a", "Money<n>b" and "Money<n>d", n from 0).
//
// A currency without such a mark has no hundredths: the stored number is written whole. The whole part is
// grouped by threes with commas, unless the currency already uses the comma for its hundredths; then it is
// not grouped. A minus goes in front of everything else.
#pragma once

#include "ui/string_table.hpp"

#include <cstdint>
#include <string>

namespace rct3::ui
{

    struct Currency
    {
        std::u16string before;
        std::u16string after;
        std::u16string decimal; ///< Before the hundredths; none for a currency without them
    };

    /// The currency at `index` in the game's list, from its strings.
    [[nodiscard]] Currency currency(const StringTable& strings, int index);

    /// `amount` in hundredths of the unit.
    [[nodiscard]] std::u16string money_text(const Currency& currency, std::int64_t amount);

} // namespace rct3::ui
