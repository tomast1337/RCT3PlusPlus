// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/awards.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace rct3::simulation
{

    namespace
    {
        constexpr std::array<std::string_view, kAwardKinds> kNames = {
            "Award_Tidiest",      "Award_Most_Untidy",    "Award_Worst_Value",     "Award_Best_Value",     "Award_Best_Food",    "Award_Worst_Food",      "Award_Most_Thrilling", "Award_Most_Disappointing",   "Award_Most_Beautiful",      "Award_Safest",       "Award_Best_Staff",        "Award_Best_Toilets", "Award_Best_Roller_Coasters", "Award_Best_Water_Rides",       "Award_Best_Reliability", "Award_Worst_Reliability", "Award_Theme_Space",
            "Award_Theme_Spooky", "Award_Theme_WildWest", "Award_Theme_Adventure", "Award_Theme_Atlantis", "Award_Theme_Island", "Award_Most_Unsanitary", "Award_Most_Dangerous", "Award_Most_Dangerous_Pools", "Award_Extremely_Dangerous", "Award_Theme_Jungle", "Award_Theme_Prehistoric", "Award_Best_Animals", "Award_Cutest_Animals",       "Award_Most_Neglected_Animals",
        };

        constexpr std::array<std::int32_t, 10> kFines = { 1, 2, 5, 7, 15, 22, 23, 24, 25, 30 };
    } // namespace

    std::string_view award_name(std::int32_t kind)
    {
        return kind >= 0 && std::cmp_less(kind, kNames.size()) ? kNames[static_cast<std::size_t>(kind)] : std::string_view { };
    }

    bool award_is_fine(std::int32_t kind)
    {
        return std::ranges::contains(kFines, kind);
    }

    std::string_view award_message(std::int32_t kind)
    {
        return award_is_fine(kind) ? "Award_Fine" : "Award_Award";
    }

} // namespace rct3::simulation
