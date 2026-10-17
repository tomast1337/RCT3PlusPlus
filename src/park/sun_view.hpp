// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view from the sun that a shadow map is drawn with: a square looked straight down the sunlight, round a
// point near the camera, deep enough to hold everything that could cast a shadow into it. Shadow edges crawl
// whenever the map is drawn from a slightly different place, so everything about the view moves in steps: the
// square in whole texels, its size in a few fixed sizes, and the sun's direction only once it has moved enough.
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

    /// How far round the camera's target shadows reach, for a camera `distance` from it: more when further out,
    /// in steps of a quarter of an octave, so zooming changes the map only now and then.
    [[nodiscard]] float shadow_radius(float distance);

    /// The sun direction to draw shadows with: `kept` until `now` (both unit length) is more than `step` radians
    /// from it, then `now`.
    [[nodiscard]] glm::vec3 steady_sun(glm::vec3 kept, glm::vec3 now, float step);

    /// How far the sun has gone from `kept` (as steady_sun keeps it) towards its next step: 0 at `kept`, 1 a
    /// whole `step` radians on. Shadows drawn at the last two kept directions mix by it, the older weighing one
    /// less this, so they move smoothly and arrive at each drawing just as the next is made.
    [[nodiscard]] float sun_step_share(glm::vec3 kept, glm::vec3 now, float step);

} // namespace rct3::park
