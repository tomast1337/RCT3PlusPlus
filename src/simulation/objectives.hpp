// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The goals a scenario sets, checked as the park runs. A scenario has up to three levels, apprentice,
// entrepreneur and tycoon, won in that order: only the first level not yet won is looked at. A level is won once
// every goal it sets has been met. A goal that must hold for some months is met once it has held that long
// without a break; once met it stays met. Winning a level gives the scenario's prize for it, then the level's own.
//
// The goals and how far each has got are kept in the park's own objective records, so the park file and the
// objectives window always agree with what is checked here.

#include "save/model/park.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::simulation
{
    struct ParkState;

    /// The rides a coaster goal counts.
    enum class RideGroup : std::uint8_t
    {
        Other,
        Coaster,
        PoolSlide,
        Water, ///< Boats, flumes and the like, for the awards
    };

    /// The group of a ride by its attraction kind.
    [[nodiscard]] RideGroup ride_group(std::uint32_t kind) noexcept;

    struct GoalRide
    {
        RideGroup group = RideGroup::Other;
        const save::Ride* ride = nullptr;
    };

    /// How the park stands, in the units the goals' targets are kept in.
    struct ParkFigures
    {
        std::int64_t park_value = 0;  ///< Hundredths of the currency unit
        std::int32_t guests = 0;      ///< In the park now
        std::int32_t rating = 0;      ///< 0 to 1000
        std::int64_t loan = 0;        ///< Hundredths
        std::int64_t ride_income = 0; ///< Last month's, hundredths
        std::int64_t shop_profit = 0; ///< Last month's, hundredths
        std::span<const GoalRide> rides { };
    };

    /// What the park took on rides and in shops in the last whole calendar month.
    struct Takings
    {
        std::int64_t month = -1;                         ///< The month being counted; -1 before counting began
        std::int64_t rides_before = 0, shops_before = 0; ///< Takings so far when that month began
        std::int64_t rides = 0, shops = 0;               ///< Last month's; 0 until a month has ended
    };

    /// Moves `takings` on to calendar `time`, given all the park has taken on rides and in shops so far.
    void count_takings(Takings& takings, float time, std::int64_t rides_so_far, std::int64_t shops_so_far);

    /// Whether the simulation can tell if a goal of `kind` is met. Those it cannot are never met.
    [[nodiscard]] bool judged(save::ConditionKind kind) noexcept;

    /// Prize numbers past the scenario's own: those of the three levels.
    inline constexpr std::int32_t kApprenticePrize = 10;

    /// The level being played: the first not yet won, 0 apprentice to 2 tycoon. It stays at tycoon once that is won.
    [[nodiscard]] std::int32_t level_playing(const save::ObjectiveSet& set);

    /// Checks the goals of the level being played at calendar `time`, marking those met. When that makes the
    /// level won, marks it and returns the prizes it gives: the scenario's for the level (0 when none), then the
    /// level's own.
    std::optional<std::array<std::int32_t, 2>> check_goals(save::ObjectiveSet& set, const ParkFigures& figures, float time);

    /// Gives the park prize `id`'s reward. The construction rights prize puts the land held back from the
    /// park on sale.
    void give_prize(ParkState& park, save::Finance& finance, save::TerrainGrid& land, std::int32_t id);

    /// Text keys of prize `id`'s name and of the message telling the player they have it.
    [[nodiscard]] std::string_view prize_name(std::int32_t id);
    [[nodiscard]] std::string_view prize_message(std::int32_t id);

} // namespace rct3::simulation
