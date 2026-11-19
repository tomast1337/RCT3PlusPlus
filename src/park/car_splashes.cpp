// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/car_splashes.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    namespace
    {
        // Chosen by eye.
        constexpr units::Pace kSplashFall { 4.0f };          ///< The car must be falling this fast, to splash where it stops
        constexpr units::Acceleration kSplashPull { 25.0f }; ///< And how sharply its fall must stop
        constexpr units::Pace kSplashRun { 3.0f };           ///< It must run this fast, to splash or leave a wake
        constexpr units::Seconds kSplashRest { 1.5f };       ///< Between splashes
        constexpr units::Seconds kWakeEvery { 0.6f };        ///< Between wakes
        constexpr units::Pace kMedium { 7.0f };              ///< A medium splash takes this speed
        constexpr units::Pace kHigh { 12.0f };               ///< And a high one
    } // namespace

    CarSplash car_splash(CarWater& water, const track::TrackCourse& course, f32 distance, bool in_water, glm::vec3 velocity, f32 seconds)
    {
        const usize pieces = std::min(course.piece_starts.size(), course.wet.size());
        const f32 length = course.length();
        if (pieces == 0 || length <= 0 || seconds <= 0)
            return { };
        if (course.closed)
            distance = std::fmod(std::fmod(distance, length) + length, length);
        const auto after = std::ranges::upper_bound(course.piece_starts.begin(), course.piece_starts.begin() + static_cast<std::ptrdiff_t>(pieces), distance);
        const usize piece = after == course.piece_starts.begin() ? 0 : static_cast<usize>(after - course.piece_starts.begin()) - 1;
        const bool wet = in_water || course.wet[piece];
        const f32 speed = glm::length(velocity);
        // Into the water from dry track, or stopped short by it at the foot of a drop; a car held at the top of a
        // spike stops short too, but stands still.
        const bool stopped = water.rise && water.rise->value < -kSplashFall.value && (velocity.y - water.rise->value) / seconds > kSplashPull.value;
        CarSplash out;
        water.rest.value = std::max(0.0f, water.rest.value - seconds);
        water.wake.value = std::max(0.0f, water.wake.value - seconds);
        if (wet && speed > kSplashRun.value && water.rest.value <= 0 && (!water.wet || stopped))
        {
            out.splash = true;
            water.rest = kSplashRest;
            ++water.splashes;
            water.strength = speed > kHigh.value ? 2 : speed > kMedium.value ? 1 : 0;
        }
        // shortcut: no wake in the park's water, where a car dives under rather than skims; upgrade for cars
        // that skim a lake.
        if (course.wet[piece] && speed > kSplashRun.value && water.wake.value <= 0)
        {
            out.wake = true;
            water.wake = kWakeEvery;
        }
        water.wet = wet;
        water.rise = units::Pace { velocity.y };
        return out;
    }

} // namespace rct3::park
