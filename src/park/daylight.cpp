// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/daylight.hpp"

#include "core/types.hpp"
#include "park/sun.hpp"

#include <algorithm>

namespace rct3::park
{

    namespace
    {

        // Heights of the sun, in degrees, over which each part of the light comes in.
        constexpr f32 kDiscFrom = -1.0f;       ///< The disc starts to clear the horizon
        constexpr f32 kDiscTo = 6.0f;          ///< And has cleared it
        constexpr f32 kWarmLightTo = 2.0f;     ///< Under this the sun's light is at its warmest
        constexpr f32 kWhiteLightFrom = 25.0f; ///< And from this at its whitest
        constexpr f32 kNightSkyTo = -12.0f;    ///< Under this the sky is the night's
        constexpr f32 kDaySkyFrom = 10.0f;     ///< And from this the day's

        /// 0 below `from`, 1 above `to` and a smooth rise between, for heights of the sun in degrees.
        f32 rise(f32 from, f32 to, f32 height)
        {
            const f32 t = std::clamp((height - from) / (to - from), 0.0f, 1.0f);
            return t * t * (3 - 2 * t);
        }

    } // namespace

    Daylight daylight(f32 time)
    {
        Daylight out;
        out.sun_direction = sun_direction(time);
        const f32 height = glm::degrees(sun_elevation(time));
        // The disc clears the horizon over a few degrees, and its light warms the lower it is.
        const f32 shown = rise(kDiscFrom, kDiscTo, height);
        out.sun = glm::mix(kLowSun, kHighSun, rise(kWarmLightTo, kWhiteLightFrom, height)) * shown;
        // The sky stays light a while after sunset and before sunrise.
        out.sky = glm::mix(kNightSky, kDaySky, rise(kNightSkyTo, kDaySkyFrom, height));
        out.ground = kGroundBounce * (out.sky + out.sun * std::max(out.sun_direction.y, 0.0f));
        return out;
    }

    Daylight mixed(const Daylight& a, const Daylight& b, f32 t)
    {
        Daylight out;
        const glm::vec3 turned = glm::mix(a.sun_direction, b.sun_direction, t);
        out.sun_direction = glm::length(turned) > 1e-4f ? glm::normalize(turned) : b.sun_direction;
        out.sun = glm::mix(a.sun, b.sun, t);
        out.sky = glm::mix(a.sky, b.sky, t);
        out.ground = glm::mix(a.ground, b.ground, t);
        return out;
    }

} // namespace rct3::park
