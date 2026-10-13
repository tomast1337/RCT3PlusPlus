// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game's text says about careers: the scenarios of each game's career, in order, as the keys
// "Scenario01_Lookup", "Scenario02_Lookup" and on name them ("AOP0_" in front for the first expansion,
// "AOP1_" for the second), up to the first number missing; and the name a new career gets, "Player"
// in the language and a number.
#pragma once

#include "ui/string_table.hpp"

#include <string>
#include <vector>

namespace rct3::ui
{

    /// The ids of game `pack`'s career scenarios: 0 the original game, then each expansion.
    [[nodiscard]] std::vector<std::string> career_scenario_ids(const StringTable& strings, int pack);

    /// "Player 3" for `number` 3, in the language.
    [[nodiscard]] std::u16string default_career_name(const StringTable& strings, std::size_t number);

} // namespace rct3::ui
