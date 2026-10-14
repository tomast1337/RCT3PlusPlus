// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/daylight.hpp"

#include "park/sun.hpp"

#include <algorithm>

namespace rct3::park
{

    namespace
    {

        /// 0 below `from`, 1 above `to` and a smooth rise between, for heights of the sun in degrees.
        float rise(float from, float to, float height)
        {
            const float t = std::clamp((height - from) / (to - from), 0.0f, 1.0f);
            return t * t * (3 - 2 * t);
        }

    } // namespace

    Daylight daylight(float time)
    {
        Daylight out;
        out.sun_direction = sun_direction(time);
        const float height = glm::degrees(sun_elevation(time));
        // The disc clears the horizon over a few degrees, and its light warms the lower it is.
        const float shown = rise(-1.0f, 6.0f, height);
        out.sun = glm::mix(kLowSun, kHighSun, rise(2.0f, 25.0f, height)) * shown;
        // The sky stays light a while after sunset and before sunrise.
        out.sky = glm::mix(kNightSky, kDaySky, rise(-12.0f, 10.0f, height));
        out.ground = kGroundBounce * (out.sky + out.sun * std::max(out.sun_direction.y, 0.0f));
        return out;
    }

    Daylight mixed(const Daylight& a, const Daylight& b, float t)
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
