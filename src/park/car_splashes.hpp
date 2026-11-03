// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cars splashing through the water of water rides. A car splashes as it runs into water, on its track or the
// park's own (a lake it dives into), or as the water stops it short at the foot of a drop: its splash points spray, its riders are soaked and the ride
// sounds a splash. Running on through water fast enough, it leaves a wake now and then.
#pragma once

#include "park/track_course.hpp"

#include <cstdint>
#include <optional>

namespace rct3::park
{

    /// What a car on a track knows of the water it runs through, from step to step.
    struct CarWater
    {
        bool wet = false;              ///< On a piece with water at the last step
        std::optional<float> rise { }; ///< How fast it rose at the last step, world units a second; none before one
        float rest = 0;                ///< Seconds before it can splash again
        float wake = 0;                ///< Seconds before its next wake
        std::uint32_t splashes = 0;    ///< Times it has splashed
        std::uint8_t strength = 0;     ///< Of the last splash: 0 low, 1 medium, 2 high, by its speed
    };

    /// What a car does in the water this step.
    struct CarSplash
    {
        bool splash = false; ///< It splashes now
        bool wake = false;   ///< It leaves a wake now
    };

    /// Moves `water` on by `seconds` for a car at `distance` along `course`, moving at `velocity`; `in_water`
    /// when it is in water its track does not carry, the park's own.
    [[nodiscard]] CarSplash car_splash(CarWater& water, const TrackCourse& course, float distance, bool in_water, glm::vec3 velocity, float seconds);

} // namespace rct3::park
