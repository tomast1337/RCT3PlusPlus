// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sun.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::park
{

    glm::vec3 sun_direction(float time)
    {
        // The hour angle: how far the sky has turned since noon.
        const float hour = 2 * std::numbers::pi_v<float> * (time - 0.5f);
        const float east = -std::cos(kSunDeclination) * std::sin(hour);
        const float north = std::cos(kParkLatitude) * std::sin(kSunDeclination) - std::sin(kParkLatitude) * std::cos(kSunDeclination) * std::cos(hour);
        const float up = std::sin(kParkLatitude) * std::sin(kSunDeclination) + std::cos(kParkLatitude) * std::cos(kSunDeclination) * std::cos(hour);
        return glm::normalize(glm::vec3(east, up, north));
    }

    float sun_elevation(float time)
    {
        return std::asin(std::clamp(sun_direction(time).y, -1.0f, 1.0f));
    }

} // namespace rct3::park
