// SPDX-License-Identifier: GPL-3.0-or-later
//
// How much a guest enjoyed a ride they just got off: the points they give it, what those do to their mood, and
// whether it becomes their favourite ride.
#pragma once

#include "simulation/guests.hpp"

#include <cstdint>
#include <vector>

namespace rct3::simulation
{

    /// The ride a guest liked best so far, and the score it got from them, on the scale saved parks keep it in.
    struct Favourite
    {
        entt::entity ride = entt::null;
        std::uint64_t saved = 0; ///< The saved park's own id of it; 0 when it is not from a save
        float score = 0;
    };

    /// The rides a guest has been on, each once, and the one they were on last.
    struct Ridden
    {
        std::vector<entt::entity> rides { };
        entt::entity last = entt::null;
    };

    /// What a guest weighs up as they step off a ride.
    struct RideTaken
    {
        units::Share excitement { }; ///< As they felt it
        units::Share intensity { };
        units::Share nausea { };
        units::Money price { }; ///< What they paid
        units::Money worth { }; ///< What a ride like it is worth to a guest
        units::Seconds waited { };
        bool new_to_them = false;
        bool eating = false; ///< Still eating or drinking: a full stomach takes a ride worse
    };

    /// How well a ride's rating sits with what a guest likes, from right inside their range to way off it.
    enum class Fit : std::uint8_t
    {
        WayOff,
        Far,
        Near,
        Within,
    };

    /// Where `rating` sits against the range `least`..`most`: near it within `slack` either side, far within twice that.
    [[nodiscard]] Fit fit(float rating, float least, float most, float slack);

    /// The nausea ratings a guest of stomach `tolerance` takes in comfort: none but the mildest for a weak one,
    /// and a strong one finds a gentle ride dull.
    [[nodiscard]] Taste stomach_range(units::Share tolerance);

    /// Points a guest in `mood` with `taste` gives the ride they took: for how well its intensity and nausea suit
    /// them, what they paid against its worth, how long they waited and whether it was new to them. A ride that
    /// suits them in every way scores 125, one that does in none -122.
    [[nodiscard]] std::int32_t enjoyment_points(const RideTaken& ride, const Taste& taste, const Mood& mood);

    /// A guest's score for a ride they gave `points`, to compare with their favourite's.
    [[nodiscard]] float favourite_score(units::Share excitement, std::int32_t points);

    /// What a ride the guest gave `points` does to them: happier (or gloomier) by its points, sicker by its
    /// nausea, the less so the happier it left them; and it is their favourite from now on when it beats the one
    /// they have, unless they are in too low a mood to think so. Returns whether it became their favourite.
    bool take_ride(Mood& mood, Favourite& favourite, entt::entity ride, std::uint64_t saved, const RideTaken& taken, std::int32_t points);

    /// A ride a guest thinks of going on.
    struct RideInMind
    {
        units::Share excitement { };
        units::Share intensity { };
        units::Share nausea { };
        units::Money price { };
        units::Money worth { }; ///< What a ride like it is worth to a guest
        bool favourite = false;
        bool just_been = false; ///< The ride they got off last
    };

    /// How much a guest in `mood` with `taste` wants to go on `ride`: for its excitement, how well its intensity
    /// suits them, its price against its worth, and whether it would sicken them; more for their favourite, less
    /// for the one they just got off. They go only on a ride that reaches the bar in the tuning.
    [[nodiscard]] float ride_appeal(const RideInMind& ride, const Taste& taste, const Mood& mood);

} // namespace rct3::simulation
