// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the simulation's tuning numbers measure. Each kind of number is its own type, so a value says what it
// is where it is declared, and one kind cannot be passed where another is meant. Use `.value` to work with the
// plain number.
#pragma once

#include <cstdint>

namespace rct3::simulation::units
{

    /// A span of game time.
    struct Seconds
    {
        float value = 0;
    };

    /// How fast something on a 0 to 1 scale (a need, happiness, nausea, wetness) moves: how much a second.
    struct PerSecond
    {
        float value = 0;
        /// The rate that takes the whole scale in `whole`.
        static constexpr PerSecond over(Seconds whole) noexcept
        {
            return { 1.0f / whole.value };
        }
    };

    /// How often something happens: times a second, so also the chance of it in one second.
    struct Frequency
    {
        double value = 0;
        /// Once every `gap`, on average.
        static constexpr Frequency every(Seconds gap) noexcept
        {
            return { 1.0 / gap.value };
        }
    };

    /// The chance of something happening, 0 to 1, each time it might.
    struct Chance
    {
        double value = 0;
    };

    /// A part of a whole, 0 to 1: of a scale (happiness, a rating), or of a group of guests.
    struct Share
    {
        float value = 0;
    };

    /// A multiplier: 1 changes nothing, 0.5 halves.
    struct Factor
    {
        double value = 1;
    };

    /// Money, in hundredths of the currency unit.
    struct Money
    {
        std::int64_t value = 0;
    };

    /// Air temperature, degrees Celsius.
    struct Degrees
    {
        float value = 0;
    };

    /// A distance in world units.
    struct Distance
    {
        float value = 0;
    };

    /// A distance along a path, in tiles.
    struct Tiles
    {
        float value = 0;
    };

    /// World units a second.
    struct Pace
    {
        float value = 0;
    };

    /// A ride's excitement, intensity or nausea, on the 0 to 10 scale rides are rated on.
    struct RideScore
    {
        float value = 0;
    };

    /// Points of the park rating, out of 1000.
    struct RatingPoints
    {
        float value = 0;
    };

} // namespace rct3::simulation::units
