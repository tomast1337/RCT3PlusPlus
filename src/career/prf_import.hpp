// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original game's career files, read to bring a career over. Such a file is a save (save/save_file.hpp)
// holding one object of class "ManagedPlayerProfile": the career's name ("PlayerName"), its game
// ("AOPIndex": -1 the original, then each expansion from 0), the scenario last picked ("CurrentScenario")
// and, per scenario in the order of the game's list, its status and last save ("ScenarioData", each with
// "Status" and "LastSaveName").
//
// Statuses there count 0 locked, 1 and 2 open, then 3 to 5 for the three levels. Scenarios are known only
// by their place, so career_from_prf() needs the game's list to name them; entries past its end are
// dropped, as the game drops them. The last save is a file name in the career's folder of games, in a
// folder per scenario named "Scenario" and its place from 01.
#pragma once

#include "career/career.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::career
{

    struct PrfScenario
    {
        std::int32_t status = 0;
        std::string last_save;
    };

    struct PrfCareer
    {
        std::string name;
        int pack = 0; ///< Ours: 0 the original game, then each expansion
        std::int32_t current = 0;
        std::vector<PrfScenario> scenarios;
    };

    enum class PrfError : std::uint8_t
    {
        NotASave,    ///< The container does not read
        NoCareer,    ///< No career object in it
        UnknownPack, ///< Of a game this program does not know
    };

    [[nodiscard]] const char* to_string(PrfError e) noexcept;

    [[nodiscard]] std::expected<PrfCareer, PrfError> read_prf(std::span<const std::byte> bytes);

    /// The career as ours, its scenarios named by `ids`, the game's list for its pack.
    [[nodiscard]] Career career_from_prf(const PrfCareer& prf, std::span<const std::string> ids);

    /// Where the game kept scenario `index`'s games, in the career's folder: "Scenario01" and on.
    [[nodiscard]] std::string prf_scenario_folder(std::size_t index);

} // namespace rct3::career
