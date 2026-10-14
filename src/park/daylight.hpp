// SPDX-License-Identifier: GPL-3.0-or-later
//
// The light the park is shaded with at a time of day, worked out from where the sun is. Three lights, all
// linear colours that the shader adds together:
//
//  - The sun, from its direction, white-gold when high and turning orange as it nears the horizon, fading
//    out as it sets.
//  - The open sky, lighting everything from above: a soft blue by day, a deep blue by night.
//  - The ground, lighting from below with what the sky and the sun put on it.
//
// None of the shades here come from the game; they are ours to tune.
#pragma once

#include "math/glm.hpp"

namespace rct3::park
{

    struct Daylight
    {
        glm::vec3 sun_direction { 0.0f, 1.0f, 0.0f }; ///< Towards the sun
        glm::vec3 sun { 0.0f };                       ///< The sun's light on a surface facing it
        glm::vec3 sky { 1.0f };                       ///< Light from the open sky above
        glm::vec3 ground { 0.0f };                    ///< Light thrown back up from the ground
    };

    inline constexpr glm::vec3 kHighSun { 0.62f, 0.6f, 0.55f };
    inline constexpr glm::vec3 kLowSun { 0.6f, 0.32f, 0.14f };
    inline constexpr glm::vec3 kDaySky { 0.42f, 0.46f, 0.55f };
    inline constexpr glm::vec3 kNightSky { 0.13f, 0.17f, 0.3f };
    /// How much of the light falling on the ground it throws back up.
    inline constexpr float kGroundBounce = 0.3f;

    /// The light at `time`, a fraction of a day from midnight.
    [[nodiscard]] Daylight daylight(float time);

    /// `a` turned towards `b` by `t`, 0 to 1: the colours mixed, the sun's direction turned between.
    [[nodiscard]] Daylight mixed(const Daylight& a, const Daylight& b, float t);

} // namespace rct3::park
