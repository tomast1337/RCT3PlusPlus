// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A ride car's part that spins round its up on the car, such as a spinning coaster's tub, or a whole car that spins,
// such as a rapids ride's raft. Free on its bearing it keeps its heading while the car turns under it; what turns it
// is its weight, off its pivot towards its back: thrown out of a turn by the car's turning, and pulled downhill on a
// slope or bank. Its spin dies down, a spring may draw it back to face ahead, and it may be held to a furthest angle
// and a fastest turn. Some are held still on a lift chain. At a station it squares up, turning slowly to the nearest
// of the ways it may stop at, or, as a raft does, slows to a stop wherever it faces.

#include "math/glm.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation
{

    /// How a spinning part comes to rest at a station.
    enum class SpinRest : std::uint8_t
    {
        Squares, ///< Turns slowly on to the nearest of its stops, and stops facing it
        Settles, ///< Slows to a stop wherever it faces
    };

    /// How a car's part spins on it, as its car file has it. Directions are in the car's own space: +Z ahead, +Y up.
    struct SpinningPart
    {
        /// From its pivot to the middle of its weight, across its up: what the car's turning and gravity turn it by.
        glm::vec3 lever { 0, 0, -0.5f };
        units::PerSecond damping { };               ///< How fast its spin dies down
        units::Share pull { };                      ///< Of the car's acceleration, the share it feels
        units::Share gravity { 1 };                 ///< Of gravity, the share it feels
        float spring = 0;                           ///< Its spring's strength over its weight: drawn back to face ahead by this times its angle
        float friction = 0;                         ///< Of the weight on its bearing, the share holding it back as it turns
        std::optional<units::TurnRate> fastest { }; ///< The fastest it spins, where it is held to one
        std::optional<units::Radians> furthest { }; ///< The furthest it turns from facing ahead, where it is held to that
        std::uint32_t stops = 1;                    ///< The ways round it may stop at a station, evenly spaced from facing ahead
        SpinRest rest = SpinRest::Squares;
        bool held_on_chains = false; ///< It is held from spinning on a lift chain
    };

    /// What a spinning part's car is on.
    enum class SpinGround : std::uint8_t
    {
        Track,
        Chain,
        Station,
    };

    /// Where a spinning part is in coming to rest at a station.
    enum class Squaring : std::uint8_t
    {
        Free,    ///< Away from a station
        Turning, ///< Turning on to a stop
        Squared, ///< At rest, till its car leaves the station
    };

    /// Where a spinning part is now: its angle from facing ahead on its car, turned as glm::rotate turns about the car's
    /// up, and how fast it spins in the world, which is not how fast that angle changes while its car turns.
    struct Spin
    {
        units::Radians angle { };
        units::TurnRate rate { };
        units::Radians was { }; ///< Its angle when the last step of the simulation began, for drawing between steps
        Squaring squaring = Squaring::Free;
    };

    /// What moves a spinning part's car: its axes in the world (columns: the car's +X, up and ahead), how hard it
    /// speeds up (world, m/s²), how fast it turns round its own up, and what it is on.
    struct SpinCar
    {
        glm::mat3 axes { 1.0f };
        glm::vec3 acceleration { };
        units::TurnRate turning { };
        SpinGround ground = SpinGround::Track;
    };

    /// Steps `spin` of `part` on by `seconds`, its car moving as `car` has it.
    void spin_on(Spin& spin, const SpinningPart& part, const SpinCar& car, float seconds);

} // namespace rct3::simulation
