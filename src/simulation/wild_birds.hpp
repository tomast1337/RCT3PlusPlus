// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wild birds: scenery that comes and goes. They have no needs, nothing to buy and no place among the zoo
// animals; they are only there to be looked at.
//
// Ducks come in flocks. A flock flies in from beyond the park in a V behind its leader, makes for a stretch of
// water (a lake or a pool), comes down on it and paddles about there, each duck keeping near the leader, the
// leader wandering a few steps at a time and only ever over water. After a while the flock takes off and either
// flies to another stretch of water or leaves the park. Guests walking up to the flock make it take off.
//
// Gulls come one at a time, more often the more litter lies about. A gull flies in from beyond the park; if
// there is litter it may come down beside a piece, stand there a while and go, perhaps to another piece, and
// otherwise it flies across and out of the park again. A gull leaves when a guest comes close.
//
// Birds are drawn with a Pose and a WildBird. Nothing about them is kept in a saved park: new ones come
// as time goes by.
#pragma once

#include "random/generator.hpp"
#include "simulation/components.hpp"
#include "simulation/entt.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace rct3::simulation
{

    enum class BirdKind : std::uint8_t
    {
        Duck,
        Gull,
    };

    enum class BirdJob : std::uint8_t
    {
        Flying,    ///< In the air, on the way to `goal`
        Landing,   ///< Coming down from `from` onto `goal`
        Resting,   ///< On the water or the ground at `goal`: a duck paddles about, a gull stands
        TakingOff, ///< Rising from `from`, headed for `goal`
    };

    /// A wild bird.
    struct WildBird
    {
        BirdKind kind = BirdKind::Duck;
        std::uint8_t palette = 0; ///< Which of its picture's colourings it wears, 0..3
        BirdJob job = BirdJob::Flying;
        glm::vec3 goal { 0.0f }; ///< Where it is headed now: its flock's goal in the air, the spot it paddles or stands at
        glm::vec3 from { 0.0f }; ///< Where a landing or take-off began
        /// The bird it follows: the one that picks where its flock goes. A leader, or a bird alone, is its own.
        entt::entity leader = entt::null;
        glm::vec2 slot { 0.0f }; ///< Where it flies in its flock, sideways and behind its leader
        units::Seconds busy { }; ///< In a landing or take-off, how long it has been at it; resting, how long is left
        bool leaving = false;    ///< Flying out of the park, not to land
        std::uint8_t visits = 0; ///< Gull: pieces of litter it has come down at
    };

    /// What the birds fly and swim over, and where the park ends.
    struct BirdGround
    {
        std::function<float(glm::vec2)> height;                                   ///< Of the ground, at a point across the park
        std::function<std::optional<float>(glm::vec2)> water;                     ///< The height of the water's surface at a point, none on land
        std::function<std::optional<glm::vec2>(random::RandomState&)> water_spot; ///< A point picked at random over water; none without water
        glm::vec2 low { 0.0f };                                                   ///< The park's least corner across the park
        glm::vec2 high { 0.0f };                                                  ///< And its greatest
    };

    /// How birds come and go, in the registry's context so a park can change them. Looks only: its dice are of the
    /// stream for effects and never draw on the guests'.
    struct WildBirds
    {
        random::RandomState dice;
        units::Seconds duck_in { 20 }; ///< Till the next flock of ducks comes in by itself
        units::Seconds gull_in { 60 }; ///< Till the next gull does
        bool automatic = true;         ///< Whether birds come by themselves
    };

    /// Values for how birds live.
    struct BirdRates
    {
        std::int32_t flock_least = 9;             ///< Ducks in a flock that comes in by itself, at least
        std::int32_t flock_most = 17;             ///< And at most
        units::Seconds flocks_every_least { 90 }; ///< Between one flock of ducks coming in and the next, at least
        units::Seconds flocks_every_most { 180 }; ///< And at most
        std::int32_t flocks_at_once = 2;          ///< Flocks of ducks at one time, past which no more come by themselves
        units::Seconds gull_spacing { 45 };       ///< Between gulls with a park all litter; a clean park waits longer
        std::int32_t gulls_at_once = 8;           ///< Gulls at one time, past which no more come by themselves
        std::int32_t gull_visits = 4;             ///< Pieces of litter a gull comes down at before it flies off for good
        units::Share gull_again { 0.6f };         ///< Of the time a gull that stood at litter makes for another piece
        units::Pace flight { 9 };                 ///< Flying
        units::Pace catching_up { 12 };           ///< A duck fallen behind its place in the V flies at
        units::Pace paddle { 1.0f };              ///< A duck swimming
        units::Distance cruise_above { 10 };      ///< Flying over the ground, this far above it
        units::Pace climb { 4 };                  ///< Rising or sinking to the height it flies at
        units::Distance approach { 14 };          ///< This far from where it will land, a bird starts to come down
        units::Distance arrived { 3 };            ///< This near its goal out of the park, a bird is gone
        units::Seconds landing { 1.1f };          ///< Coming down, as long as the landing motion
        units::Seconds taking_off { 1.1f };       ///< Rising, as long as the take-off motion
        units::Distance side { 1 };               ///< Between neighbours in the V, sideways
        units::Distance behind { 0.8f };          ///< And behind
        units::Distance huddle { 2.5f };          ///< How far ducks on the water keep from their leader
        units::Distance wander { 4 };             ///< Farthest a leader paddles at a time
        units::Seconds stay_least { 25 };         ///< A flock on the water, at least
        units::Seconds stay_most { 70 };          ///< And at most
        units::Share hop { 0.5f };                ///< Of the flocks that take off, those that make for other water
        units::Distance hop_least { 20 };         ///< Other water at least this far from where they were
        units::Distance duck_scare { 7 };         ///< A guest this near makes ducks take off
        units::Distance gull_scare { 1.5f };      ///< And a gull
        units::Seconds gull_stands_least { 6 };   ///< A gull at litter, at least
        units::Seconds gull_stands_most { 15 };   ///< And at most
        units::Distance beyond { 40 };            ///< Birds come in from, and leave to, this far outside the park's corners
        std::int32_t tries = 8;                   ///< Spots picked before a bird settles for staying where it is
    };

    inline constexpr BirdRates kBirdRates { };

    /// A flock of `count` ducks coming in from beyond the park for some water, the first the leader. None when the
    /// park has no water. Returns how many.
    std::size_t add_ducks(SimWorld& world, const BirdGround& ground, std::size_t count);

    /// `count` gulls coming in from beyond the park, together. Returns how many.
    std::size_t add_gulls(SimWorld& world, const BirdGround& ground, std::size_t count);

    /// Takes every wild bird out of the park. Returns how many.
    std::size_t remove_wild_birds(SimWorld& world);

    /// Moves the birds on by `dt`, and lets new ones come when it is time.
    void step_wild_birds(SimWorld& world, const BirdGround& ground, units::Seconds dt);

} // namespace rct3::simulation
