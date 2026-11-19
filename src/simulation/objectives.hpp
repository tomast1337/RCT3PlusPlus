// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The goals a scenario sets, checked as the park runs. A scenario has up to three levels, apprentice,
// entrepreneur and tycoon, won in that order: only the first level not yet won is looked at. A level is won once
// every goal it sets has been met. A goal that must hold for some months is met once it has held that long
// without a break; once met it stays met. A goal with a date is judged only on that date: the player is warned a
// month and a week before, told when it is met, and on a miss the date moves a year on. Winning a level gives the scenario's prize for it, then the level's own.
//
// The goals and how far each has got are kept in the park's own objective records, so the park file and the
// objectives window always agree with what is checked here.

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/park/messages.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::simulation
{
    struct Research;
    namespace park
    {
        struct ParkState;
    } // namespace park

    /// The rides a coaster goal counts.
    enum class RideGroup : u8
    {
        Other,
        Coaster,
        PoolSlide,
        Water, ///< Boats, flumes and the like, for the awards
    };

    /// The group of a ride by its attraction kind.
    [[nodiscard]] RideGroup ride_group(u32 kind) noexcept;

    struct GoalRide
    {
        RideGroup group = RideGroup::Other;
        const save::model::Ride* ride = nullptr;
    };

    /// How the park stands, in the units the goals' targets are kept in.
    struct ParkFigures
    {
        units::Money park_value { };
        i32 guests = 0; ///< In the park now
        i32 rating = 0; ///< 0 to 1000
        units::Money loan { };
        /// Last month's ledger period, for the goals on ride income (its ride tickets) and shop profit (shop
        /// and food sales less their stock); all nought before a month has ended.
        save::model::LedgerPeriod last_month { };
        i32 animal_health = 0; ///< The animals' average, 0 to 100; 0 with no animals
        i32 dung = 0;          ///< Piles of dung lying in the park
        std::span<const GoalRide> rides { };
    };

    /// How a goal stands against the park's figures.
    struct GoalReading
    {
        std::optional<i64> now { }; ///< The park's figure, in the goal's units; none for goals without one
        bool met = false;           ///< Met as the park stands, however long it has held
    };

    /// Reads goal `c` against `figures`. A goal the simulation cannot judge reads as unmet with no figure.
    [[nodiscard]] GoalReading read_goal(const save::model::Condition& c, const ParkFigures& figures);

    /// Months goal `c` has been met without a break at calendar `time`; 0 while it is not.
    [[nodiscard]] f32 months_held(const save::model::Condition& c, f32 time) noexcept;

    /// Whether the simulation can tell if a goal of `kind` is met. Those it cannot are never met.
    [[nodiscard]] bool judged(save::model::ConditionKind kind) noexcept;

    /// Prize numbers past the scenario's own: those of the three levels.
    inline constexpr i32 kApprenticePrize = 10;

    /// The level being played: the first not yet won, 0 apprentice to 2 tycoon. It stays at tycoon once that is won.
    [[nodiscard]] i32 level_playing(const save::model::ObjectiveSet& set);

    /// Checks the goals of the level being played at calendar `time`, marking those met, and posts what dated
    /// goals tell the player to `messages`. When that makes the level won, marks it and returns the prizes it
    /// gives: the scenario's for the level (0 when none), then the level's own.
    std::optional<std::array<i32, 2>> check_goals(save::model::ObjectiveSet& set, const ParkFigures& figures, f32 time, std::optional<park::ParkMessages&> messages = std::nullopt);

    /// Text key naming a goal of `kind`; empty for kinds without one.
    [[nodiscard]] std::string_view condition_name(save::model::ConditionKind kind) noexcept;

    /// Gives the park prize `id`'s reward, cash booked as prize money in `finance`'s ledger (`kept` as for
    /// park::book). The construction rights prize puts the land held back from the park on sale; the research prize
    /// adds to `research`'s boost, counted with the next part of the budget paid.
    void give_prize(park::ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, save::model::TerrainGrid& land, Research& research, i32 id);

    /// Text keys of prize `id`'s name and of the message telling the player they have it.
    [[nodiscard]] std::string_view prize_name(i32 id);
    [[nodiscard]] std::string_view prize_message(i32 id);

} // namespace rct3::simulation
