// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/awards.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
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

    bool award_earned(std::int32_t kind, const AwardFigures& f, std::uint32_t lately, random::RandomState& dice)
    {
        const auto had = [&](std::initializer_list<std::int32_t> kinds) { return std::ranges::any_of(kinds, [&](std::int32_t k) { return (lately & (1u << k)) != 0; }); };
        const auto count = [&](std::int32_t thought) {
            const auto found = f.thoughts.find(thought);
            return found != f.thoughts.end() ? found->second : 0;
        };
        const auto share = [&](std::int32_t thought) { return f.guests > 0 ? static_cast<float>(count(thought)) / static_cast<float>(f.guests) : 0.0f; };
        const auto rand = [&] { return static_cast<float>(random::unit(dice)); };
        // Litter, vandalism and sick lying about, as guests see it.
        const std::int32_t litter = count(3) + count(14) + count(15);
        const float litter_share = share(3) + share(14) + share(15);
        const auto rides = static_cast<std::int32_t>(f.downtime.size());
        const auto down_over = [&](float percent) { return static_cast<std::int32_t>(std::ranges::count_if(f.downtime, [&](float d) { return d > percent; })); };
        switch (kind)
        {
            case 0: // Tidiest
                return !had({ 1, 7 }) && f.guests >= 200 && static_cast<float>(f.guests) >= rand() * 500 && f.guests >= 10 * f.old_litter && share(6) > 1.0f / 64 && litter <= 5;
            case 1: // Most untidy
                return !had({ 0, 8, 10 }) && litter_share >= 1.0f / 16;
            case 2: // Worst value
                return !had({ 3 }) && f.fee_charged && f.entrance_fee > f.fair_fee;
            case 3: // Best value
                return !had({ 2, 7 }) && f.fee_charged && f.fair_fee >= 100 && static_cast<float>(f.entrance_fee + 10) < static_cast<float>(f.fair_fee) * 0.5f;
            case 4: // Best food
                return !had({ 5 }) && share(4) <= 0.25f && f.food_stalls >= 7 && f.food_kinds >= 4 && f.stalls * 128 >= f.guests;
            case 5: // Worst food
                return !had({ 4 }) && f.food_kinds <= 4 && f.stalls * 128 <= f.guests && share(4) > 0.3f;
            case 6: // Most thrilling
                return !had({ 7 }) && f.thrilling >= 6;
            case 7: // Most disappointing
            {
                float let_down = 0;
                for (const std::int32_t thought : { 182, 105, 27, 40, 44, 45, 180, 14 })
                    let_down += share(thought);
                return !had({ 3 }) && f.rating <= 500 && let_down > 0.25f;
            }
            case 8: // Most beautiful
                return !had({ 1, 7 }) && share(8) > 1.0f / 128 && litter <= 15;
            case 10: // Best staff
                return !had({ 1 }) && litter <= 10 && f.every_staff_kind && f.staff * 32 > f.guests && down_over(25) == 0;
            case 11: // Best toilets
                return f.toilets >= 4 && f.toilets * 128 >= f.guests && share(12) < 0.25f;
            case 12: // Best roller coasters
                return !had({ 7 }) && f.coasters >= 6;
            case 13: // Best water rides
                return f.water_rides >= 6;
            case 15: // Worst reliability
                return !had({ 14 }) && rides >= 3 && static_cast<float>(rides) >= rand() * 10 && (down_over(75) > 2 || down_over(30) > 6 || f.reliability < 40);
            case 22: // Most unsanitary
                return count(180) > 15;
            default:
                // shortcut: safety, best reliability, scenery themes, animals and the dangerous-event fines are never
                // given, as what they count is not known well enough or not simulated; add them as it is.
                return false;
        }
    }

    std::vector<std::int32_t> award_month(save::Awards& awards, const AwardFigures& figures, random::RandomState& dice)
    {
        if (awards.months_before_start > 0)
        {
            --awards.months_before_start;
            return { };
        }
        // The last six months, this one first; what the five before it had keeps those away.
        constexpr std::size_t kMonthsKept = 6;
        awards.earned.insert(awards.earned.begin(), std::vector<std::int32_t> { });
        awards.earned.resize(kMonthsKept);
        std::uint32_t lately = 0;
        for (std::size_t m = 1; m < awards.earned.size(); ++m)
            for (const std::int32_t k : awards.earned[m])
                if (k >= 0 && k < kAwardKinds)
                    lately |= 1u << k;
        std::vector<std::int32_t> given;
        for (std::int32_t kind = 0; kind < kAwardKinds; ++kind)
        {
            if ((lately & (1u << kind)) != 0 || (awards.available_mask & (1u << kind)) == 0 || !award_earned(kind, figures, lately, dice))
                continue;
            awards.earned.front().push_back(kind);
            lately |= 1u << kind;
            given.push_back(kind);
        }
        // Dangerous events are forgotten in time, those at pools sooner.
        if (awards.dangerous_pool_events && *awards.dangerous_pool_events > 0 && random::chance(dice, 0.5))
            --*awards.dangerous_pool_events;
        if (awards.dangerous_coaster_events && *awards.dangerous_coaster_events > 0 && random::chance(dice, 0.05))
            --*awards.dangerous_coaster_events;
        return given;
    }

} // namespace rct3::simulation
