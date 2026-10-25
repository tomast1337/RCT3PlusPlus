// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game's text says about careers: the scenarios of each game's career, in order, as the keys
// "Scenario01_Lookup", "Scenario02_Lookup" and on name them ("AOP0_" in front for the first expansion,
// "AOP1_" for the second), up to the first number missing; and the name a new career gets, "Player"
// in the language and a number.
//
// Each scenario's id leads to the rest: its name and description ("Scenario<id>_Title", "_Text"), its
// round picture ("ScenarioIcon_<id>"), the large picture and the piece drawn over it (by number in the
// original game, "CareerMap_LargeImage<n>" and "CareerMap_LargeImageForeground<n>"; by id in the
// expansions, "CareerMap_LargeImage_<id>" and "CareerMap_LargeImageFG_<id>"), and the part of its park
// file's name ("<id>_Filename"). An expansion keeps its files in a folder of its own, named by
// "AOP_FSName_<n>": its parks under "Campaigns/Base" and its pictures under "Slideshow", where each
// scenario has a folder "Scenario<n>", numbered from 1.
#pragma once

#include "ui/string_table.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// The ids of game `pack`'s career scenarios: 0 the original game, then each expansion.
    [[nodiscard]] std::vector<std::string> career_scenario_ids(const StringTable& strings, int pack);

    struct CareerScenarioInfo
    {
        std::string id;
        std::u16string title;
        std::u16string description;
        std::string icon;
        std::string picture;
        std::string picture_front;
        std::string slideshow; ///< Folder of its pictures, relative to the game
        std::string file_part; ///< Part of the name of its park file
    };

    /// Scenario `index` of game `pack`, whose id is `id`.
    [[nodiscard]] CareerScenarioInfo career_scenario(const StringTable& strings, int pack, std::size_t index, std::string_view id);

    /// What a design locked behind base game scenario `scenario` (by place) says: the scenario to complete there
    /// and the level, 0 apprentice to 2 tycoon, to reach.
    [[nodiscard]] std::u16string locked_design_help(const StringTable& strings, std::int32_t scenario, std::int32_t level);

    /// The folder of game `pack`'s career parks, relative to the game.
    [[nodiscard]] std::string career_park_folder(const StringTable& strings, int pack);

    /// The first file in `files` (names, in any order) whose name holds `part`, ignoring case, as the game
    /// finds a scenario's park; the order is the list's.
    [[nodiscard]] std::optional<std::size_t> find_career_park(std::span<const std::filesystem::path> files, std::string_view part);

    /// "Player 3" for `number` 3, in the language.
    [[nodiscard]] std::u16string default_career_name(const StringTable& strings, std::size_t number);

} // namespace rct3::ui
