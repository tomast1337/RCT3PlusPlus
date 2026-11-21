// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's rating, our own model. Once a game week the park is scored from how it stands, and the rating
// moves part of the way towards that score, so one bad week dents it and a run of them sinks it.
//
// The score is out of 1000 and adds up five parts, each worth up to its own share:
//
//  - Happiness: how happy the guests in the park are, on average. An empty park scores half of this part.
//  - Rides: how good the tested rides are, on average, times how many rides there are to choose from. A
//    ride is good when it is exciting without being too intense.
//  - Reliability: how much of the time the rides are running.
//  - Guests: a busy park scores more, with less to gain from each guest as the park fills.
//  - Litter: litter and vomit left lying take some off, up to a cap.
//  - Lost guests: guests cut off from their party take some off, up to a cap.
//  - Awards: each award of the last months adds some, each fine takes some off.
//  - Prize: what a prize for winning a scenario level adds, fading week by week.
//
// A scenario that asks for a harder rating scales the score down.
//
// Every number here is our own, tuned so that the scores of the game's parks land near the ratings their
// saves hold.
#pragma once

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rct3::simulation::park
{

    inline constexpr i32 kMaxRating = 1000;

    /// What the rating needs from one ride.
    struct RideRatingInput
    {
        f32 downtime_percent = 0;      ///< 0..100 of the time the ride is out of action
        std::optional<f32> excitement; ///< None while the ride has no valid test result
        std::optional<f32> intensity;
    };

    /// Everything the weekly score is worked out from.
    struct RatingInput
    {
        bool harder = false; ///< The scenario asks for a harder rating
        i32 guests = 0;
        i32 guests_inside = 0; ///< Guests walked through, counted one by one
        f32 happiness = 0;     ///< Their happiness, each 0..1, added up
        std::vector<RideRatingInput> rides;
        i32 old_litter = 0; ///< Pieces lying long enough to count, vomit too
        i32 lost_guests = 0;
        i32 awards = 0; ///< Given in the months the park keeps
        i32 fines = 0;
        f32 prize = 0; ///< Points a prize adds (ParkState::rating_bonus)
    };

    /// The parts of a score, each already signed as it adds to the total.
    struct RatingParts
    {
        i32 happiness = 0;
        i32 rides = 0;
        i32 reliability = 0;
        i32 guests = 0;
        i32 litter = 0;      ///< Zero or negative
        i32 lost_guests = 0; ///< Zero or negative
        i32 awards = 0;      ///< Awards less fines
        i32 prize = 0;
        i32 harder = 0; ///< Zero or negative: what the harder rating took off
        i32 total = 0;  ///< The parts added up, kept to 0..kMaxRating

        bool operator==(const RatingParts&) const = default;
    };

    [[nodiscard]] RatingParts rate_park(const RatingInput& input);

    /// The score's inputs gathered from the park's people, rides and awards at calendar time `now`. The guest
    /// count, the harder flag and the prize are left for the caller.
    [[nodiscard]] RatingInput rating_input(const save::model::PeopleModel& people, const save::model::RideModel& rides, const save::model::Awards& awards, f32 now);

    /// The rating after a week: `before` (0..1, none for a park not rated yet) moved part of the way towards
    /// `score` (0..kMaxRating).
    [[nodiscard]] f32 next_rating(std::optional<f32> before, i32 score) noexcept;

} // namespace rct3::simulation::park
