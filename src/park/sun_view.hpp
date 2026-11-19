// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view from the sun that a shadow map is drawn with: a square looked straight down the sunlight, round a
// point near the camera, deep enough to hold everything that could cast a shadow into it. Shadow edges crawl
// whenever the map is drawn from a slightly different place, so everything about the view moves in steps: the
// square in whole texels, its size in a few fixed sizes, and the sun's direction only once it has moved enough.
// The square's steps are a sixteenth of it, so a map of what stands still serves while the camera pans within one.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "units/units.hpp"

#include <cstdint>

namespace rct3::park
{

    /// Half the depth of the view, in world units, either side of the point it is round.
    inline constexpr f32 kSunViewDepth = 600;
    /// The square moves in steps of this share of its width. Chosen by eye: fewer steps keep a still map longer
    /// while panning but spread the map's texels over more ground than the shadows need.
    inline constexpr f32 kSunViewSteps = 16;

    struct SunView
    {
        glm::mat4 view_projection { 1.0f }; ///< World to the map's clip space; depth 0 is nearest the sun
        units::Distance texel { 1 };        ///< Across one texel of the map
    };

    /// The view down `to_sun` (towards the sun, any length) over a square at least `2 * radius` across round
    /// `centre`, for a map `texels` wide; it stays put while `centre` moves within a step.
    [[nodiscard]] SunView sun_view(glm::vec3 to_sun, glm::vec3 centre, f32 radius, u32 texels);

    /// How far round the camera's target shadows reach, for a camera `distance` from it: more when further out,
    /// in steps of a quarter of an octave, so zooming changes the map only now and then.
    [[nodiscard]] f32 shadow_radius(f32 distance);

    /// The sun direction to draw shadows with: `kept` until `now` (both unit length) is more than `step` radians
    /// from it, then `now`.
    [[nodiscard]] glm::vec3 steady_sun(glm::vec3 kept, glm::vec3 now, f32 step);

    /// How far the sun has gone from `kept` (as steady_sun keeps it) towards its next step: 0 at `kept`, 1 a
    /// whole `step` radians on. Shadows drawn at the last two kept directions mix by it, the older weighing one
    /// less this, so they move smoothly and arrive at each drawing just as the next is made.
    [[nodiscard]] f32 sun_step_share(glm::vec3 kept, glm::vec3 now, f32 step);

} // namespace rct3::park
