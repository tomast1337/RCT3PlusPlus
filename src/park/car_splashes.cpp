// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/car_splashes.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    namespace
    {
        // Chosen by eye.
        constexpr float kSplashFall = 4.0f;  ///< World units a second the car must be falling, to splash where it stops
        constexpr float kSplashPull = 25.0f; ///< And how sharply its fall must stop, world units a second squared
        constexpr float kSplashRun = 3.0f;   ///< World units a second it must run at, to splash or leave a wake
        constexpr float kSplashRest = 1.5f;  ///< Seconds between splashes
        constexpr float kWakeEvery = 0.6f;   ///< Seconds between wakes
        constexpr float kMedium = 7.0f;      ///< World units a second a medium splash takes
        constexpr float kHigh = 12.0f;       ///< And a high one
    } // namespace

    CarSplash car_splash(CarWater& water, const TrackCourse& course, float distance, bool in_water, glm::vec3 velocity, float seconds)
    {
        const std::size_t pieces = std::min(course.piece_starts.size(), course.wet.size());
        const float length = course.length();
        if (pieces == 0 || length <= 0 || seconds <= 0)
            return { };
        if (course.closed)
            distance = std::fmod(std::fmod(distance, length) + length, length);
        const auto after = std::ranges::upper_bound(course.piece_starts.begin(), course.piece_starts.begin() + static_cast<std::ptrdiff_t>(pieces), distance);
        const std::size_t piece = after == course.piece_starts.begin() ? 0 : static_cast<std::size_t>(after - course.piece_starts.begin()) - 1;
        const bool wet = in_water || course.wet[piece];
        const float speed = glm::length(velocity);
        // Into the water from dry track, or stopped short by it at the foot of a drop; a car held at the top of a
        // spike stops short too, but stands still.
        const bool stopped = water.rise && *water.rise < -kSplashFall && (velocity.y - *water.rise) / seconds > kSplashPull;
        CarSplash out;
        water.rest = std::max(0.0f, water.rest - seconds);
        water.wake = std::max(0.0f, water.wake - seconds);
        if (wet && speed > kSplashRun && water.rest <= 0 && (!water.wet || stopped))
        {
            out.splash = true;
            water.rest = kSplashRest;
            ++water.splashes;
            water.strength = speed > kHigh ? 2 : speed > kMedium ? 1 : 0;
        }
        // shortcut: no wake in the park's water, where a car dives under rather than skims; upgrade for cars
        // that skim a lake.
        if (course.wet[piece] && speed > kSplashRun && water.wake <= 0)
        {
            out.wake = true;
            water.wake = kWakeEvery;
        }
        water.wet = wet;
        water.rise = velocity.y;
        return out;
    }

} // namespace rct3::park
