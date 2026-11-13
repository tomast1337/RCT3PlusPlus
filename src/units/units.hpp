// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a number measures, as its type: each kind of number is its own type, so a value says what it is where it
// is declared, and one kind cannot be passed where another is meant. Use `.value` to work with the plain number.
// Each is marked kUnit, so the park file keeps it as its bare number.
#pragma once

#include <cstdint>

namespace rct3::units
{

    /// A span of time. Spans add, take away and compare as they are; scaling one goes through `.value`.
    struct Seconds
    {
        static constexpr bool kUnit = true;
        float value = 0;
        friend constexpr auto operator<=>(Seconds, Seconds) noexcept = default;
        friend constexpr Seconds operator+(Seconds a, Seconds b) noexcept
        {
            return { a.value + b.value };
        }
        friend constexpr Seconds operator-(Seconds a, Seconds b) noexcept
        {
            return { a.value - b.value };
        }
        constexpr Seconds& operator+=(Seconds b) noexcept
        {
            value += b.value;
            return *this;
        }
    };

    /// How fast something on a 0 to 1 scale (a need, happiness, nausea, wetness) moves: how much a second.
    struct PerSecond
    {
        static constexpr bool kUnit = true;
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
        static constexpr bool kUnit = true;
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
        static constexpr bool kUnit = true;
        double value = 0;
    };

    /// A part of a whole, 0 to 1: of a scale (happiness, a rating), or of a group of guests.
    struct Share
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// A multiplier: 1 changes nothing, 0.5 halves.
    struct Factor
    {
        static constexpr bool kUnit = true;
        double value = 1;
    };

    /// Money, in hundredths of the currency unit. Sums of money add, take away and compare as they are; scaling one
    /// goes through `.value`, so each place picks its own rounding.
    struct Money
    {
        static constexpr bool kUnit = true;
        std::int64_t value = 0;
        friend constexpr auto operator<=>(Money, Money) noexcept = default;
        friend constexpr Money operator+(Money a, Money b) noexcept
        {
            return { a.value + b.value };
        }
        friend constexpr Money operator-(Money a, Money b) noexcept
        {
            return { a.value - b.value };
        }
        constexpr Money operator-() const noexcept
        {
            return { -value };
        }
        constexpr Money& operator+=(Money b) noexcept
        {
            value += b.value;
            return *this;
        }
        constexpr Money& operator-=(Money b) noexcept
        {
            value -= b.value;
            return *this;
        }
    };

    /// Air temperature, degrees Celsius.
    struct Degrees
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// A distance in world units.
    struct Distance
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// A distance along a path, in tiles.
    struct Tiles
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// An angle, in radians.
    struct Radians
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// World units a second.
    struct Pace
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// How fast something turns: radians a second.
    struct TurnRate
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// A ride's excitement, intensity or nausea, on the 0 to 10 scale rides are rated on.
    struct RideScore
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

    /// Points of the park rating, out of 1000.
    struct RatingPoints
    {
        static constexpr bool kUnit = true;
        float value = 0;
    };

} // namespace rct3::units
