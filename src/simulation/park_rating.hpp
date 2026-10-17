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
//  - Litter: litter left lying takes some off, up to a cap.
//
// A scenario that asks for a harder rating scales the score down.
//
// Every number here is our own, tuned so that the scores of the game's parks land near the ratings their
// saves hold.
#pragma once

#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rct3::simulation
{

    inline constexpr std::int32_t kMaxRating = 1000;

    /// What the rating needs from one ride.
    struct RideRatingInput
    {
        float downtime_percent = 0;      ///< 0..100 of the time the ride is out of action
        std::optional<float> excitement; ///< None while the ride has no valid test result
        std::optional<float> intensity;
    };

    /// Everything the weekly score is worked out from.
    struct RatingInput
    {
        bool harder = false;            ///< The scenario asks for a harder rating
        std::int32_t guests = 0;        ///< The park's guest count
        std::int32_t guests_inside = 0; ///< Guests walked through, counted one by one
        float happiness = 0;            ///< Their happiness, each 0..1, added up
        std::vector<RideRatingInput> rides;
        std::int32_t old_litter = 0; ///< Pieces lying long enough to count
    };

    /// The parts of a score, each already signed as it adds to the total.
    struct RatingParts
    {
        std::int32_t happiness = 0;
        std::int32_t rides = 0;
        std::int32_t reliability = 0;
        std::int32_t guests = 0;
        std::int32_t litter = 0; ///< Zero or negative
        std::int32_t harder = 0; ///< Zero or negative: what the harder rating took off
        std::int32_t total = 0;  ///< The parts added up, kept to 0..kMaxRating

        bool operator==(const RatingParts&) const = default;
    };

    /// How long a piece of litter must lie, in calendar units, before it counts against the rating: a day.
    inline constexpr float kLitterCountsAfter = save::GameClock::kUnitsPerWeek / 7;

    [[nodiscard]] RatingParts rate_park(const RatingInput& input);

    /// The score's inputs gathered from the park's people and rides at calendar time `now`. The guest count
    /// and the harder flag are left for the caller.
    [[nodiscard]] RatingInput rating_input(const save::PeopleModel& people, const save::RideModel& rides, float now);

    /// The rating after a week: `before` (0..1, none for a park not rated yet) moved part of the way towards
    /// `score` (0..kMaxRating).
    [[nodiscard]] float next_rating(std::optional<float> before, std::int32_t score) noexcept;

} // namespace rct3::simulation
