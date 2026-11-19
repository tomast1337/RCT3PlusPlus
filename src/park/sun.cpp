// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sun.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::park
{

    glm::vec3 sun_direction(f32 time)
    {
        // The hour angle: how far the sky has turned since noon.
        const f32 hour = 2 * std::numbers::pi_v<f32> * (time - 0.5f);
        const f32 east = -std::cos(kSunDeclination) * std::sin(hour);
        const f32 north = std::cos(kParkLatitude) * std::sin(kSunDeclination) - std::sin(kParkLatitude) * std::cos(kSunDeclination) * std::cos(hour);
        const f32 up = std::sin(kParkLatitude) * std::sin(kSunDeclination) + std::cos(kParkLatitude) * std::cos(kSunDeclination) * std::cos(hour);
        return glm::normalize(glm::vec3(east, up, north));
    }

    f32 sun_elevation(f32 time)
    {
        return std::asin(std::clamp(sun_direction(time).y, -1.0f, 1.0f));
    }

} // namespace rct3::park
