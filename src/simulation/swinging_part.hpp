// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A ride car's part that swings on it, such as a suspended coaster's car under its bogie, a bobsleigh's body or a
// chairlift's seat. It hangs from a pivot and swings about a line through it as a weight on an arm: gravity pulls it
// down, and the car's turning and speeding up and slowing throw it the other way, as much as its car file says it
// feels them. Its swing dies down, it is drawn back to rest, a bearing's friction slows it, and it may be held to a
// furthest angle and a fastest turn.

#include "math/glm.hpp"
#include "units/units.hpp"

#include <optional>

namespace rct3::simulation
{

    /// How a car's part swings on it, as its car file has it. Directions are in the car's own space: +Z ahead, +Y up.
    struct SwingingPart
    {
        glm::vec3 line { 0, 0, 1 }; ///< Through its pivot, that it swings about, a unit vector
        /// Its up where it is sprung to rest on the car rather than hanging level, a unit vector.
        glm::vec3 up { 0, 1, 0 };
        units::Distance arm { 1 };     ///< From its pivot to the weight that swings it
        units::PerSecond damping { };  ///< How fast its swing dies down
        units::PerSecond centring { }; ///< How fast it is drawn back to rest
        units::Share pull { };         ///< Of the car's acceleration, the share it feels
        /// Its spring's strength over its weight: held to rest on the car by this times the angle squared. With
        /// none it hangs level at rest, its up as near the world's as its line lets it.
        float spring = 0;
        float friction = 0;                         ///< Of the weight on its bearing, the share holding it back as it turns
        std::optional<units::TurnRate> fastest { }; ///< The fastest it turns, where it is held to one
        std::optional<units::Radians> furthest { }; ///< The furthest it swings from rest, where it is held to that
    };

    /// Where a swinging part is now: its angle from rest, turned as glm::rotate turns about its line, and how fast
    /// that changes.
    struct Swing
    {
        units::Radians angle { };
        units::TurnRate rate { };
        units::Radians was { }; ///< Its angle when the last step of the simulation began, for drawing between steps
    };

    /// Steps `swing` of `part` on by `seconds`, its car's axes in the world `car` (columns: the car's +X, up and
    /// ahead) and the car speeding up at `acceleration` (world, m/s²).
    void swing_on(Swing& swing, const SwingingPart& part, const glm::mat3& car, glm::vec3 acceleration, float seconds);

} // namespace rct3::simulation
