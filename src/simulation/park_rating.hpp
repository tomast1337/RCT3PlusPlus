// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's rating, worked out again each game week from how the park stands at that moment. It is not
// carried over from the week before: it adds up a handful of parts, each scored on its own, and keeps the sum
// between 0 and 1000.
//
//  - Guests: more guests in the park score more, up to a cap reached at a couple of thousand.
//  - Happy guests: the share of the guests inside who are more happy than not. An empty park gets a middling
//    score for this part rather than none.
//  - Ride up-time: how much of the time the rides are running, scaled down while the park has only a few.
//  - Excitement and intensity: the rated rides' scores added up, up to a cap, plus a little more when the
//    average ride sits near a pleasant level of each.
//  - Litter: every piece left lying for more than a few weeks takes some off, up to a cap.
//  - A harder rating, set by the scenario, starts the sum lower.
//  - A penalty and a bonus, which fade a little each week.
#pragma once

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

    /// Everything the weekly rating is worked out from.
    struct RatingInput
    {
        bool harder = false;            ///< The scenario asks for a harder rating
        std::int32_t guests = 0;        ///< The park's guest count
        std::int32_t guests_inside = 0; ///< Guests walked through, counted one by one
        std::int32_t happy_guests = 0;  ///< Of those, the ones happier than half way
        std::vector<RideRatingInput> rides;
        std::int32_t old_litter = 0; ///< Pieces lying long enough to count
        std::int32_t penalty = 0;
        std::int32_t bonus = 0;
    };

    /// The parts of a rating, each already signed as it adds to the total.
    struct RatingParts
    {
        std::int32_t base = 0; ///< Negative for a harder rating, otherwise zero
        std::int32_t guests = 0;
        std::int32_t happy_guests = 0;
        std::int32_t ride_up_time = 0;
        std::int32_t ride_scores = 0;
        std::int32_t litter = 0;  ///< Zero or negative
        std::int32_t penalty = 0; ///< Zero or negative
        std::int32_t bonus = 0;
        std::int32_t total = 0; ///< The parts added up, kept to 0..kMaxRating

        bool operator==(const RatingParts&) const = default;
    };

    /// How long a piece of litter must lie, in calendar units, before it counts against the rating.
    inline constexpr float kLitterCountsAfter = 240.0f;

    [[nodiscard]] RatingParts rate_park(const RatingInput& input);

    /// The rating's inputs gathered from the park's people and rides at calendar time `now`. The guest
    /// count, flag, penalty and bonus are left for the caller.
    [[nodiscard]] RatingInput rating_input(const save::PeopleModel& people, const save::RideModel& rides, float now);

    /// The penalty and bonus after a week has been rated: each fades by a fixed step, never below zero. The
    /// penalty fades faster in a park whose money never runs out.
    struct RatingCarry
    {
        std::int32_t penalty = 0;
        std::int32_t bonus = 0;
    };
    [[nodiscard]] RatingCarry fade(RatingCarry carry, bool unlimited_money) noexcept;

} // namespace rct3::simulation
