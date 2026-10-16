// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view from the sun that a shadow map is drawn with: a square looked straight down the sunlight, round a
// point near the camera, deep enough to hold everything that could cast a shadow into it. The square moves in
// whole texels of the map, so shadow edges hold still while the camera pans.
#pragma once

#include "math/glm.hpp"

#include <cstdint>

namespace rct3::park
{

    /// Half the depth of the view, in world units, either side of the point it is round.
    inline constexpr float kSunViewDepth = 600;

    struct SunView
    {
        glm::mat4 view_projection { 1.0f }; ///< World to the map's clip space; depth 0 is nearest the sun
        float texel = 1;                    ///< World units across one texel of the map
    };

    /// The view down `to_sun` (towards the sun, any length) over a square `2 * radius` across round `centre`, for a
    /// map `texels` wide.
    [[nodiscard]] SunView sun_view(glm::vec3 to_sun, glm::vec3 centre, float radius, std::uint32_t texels);

    /// How far round the camera's target shadows reach, for a camera `distance` from it: more when further out.
    [[nodiscard]] float shadow_radius(float distance);

} // namespace rct3::park
