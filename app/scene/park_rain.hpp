// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rain falling round the camera: streaks cut from the game's rain texture, each a quad along its fall that
// turns to face the eye. The drops keep their places in the world as the camera moves, wrapping round a box
// about the eye, and lean with the wind. How many fall follows how hard it rains.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "render/types.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace rct3::scene
{

    /// A corner of a streak's quad, as drawn.
    struct RainCorner
    {
        glm::vec3 position { 0.0f };
        glm::vec4 colour { 0.0f };
        glm::vec2 uv { 0.0f };
    };

    struct ParkRain
    {
        render::TextureHandle streak;
        render::PipelineHandle pipeline;
        std::vector<RainCorner> corners; ///< Filled again each frame, keeping its memory
    };

    /// What the rain is drawn with now.
    struct RainView
    {
        glm::mat4 view_projection { 1.0f };
        glm::vec3 eye { 0.0f };
        float amount = 0;  ///< 0 none to 1 a downpour
        float wind = 0;    ///< 0 calm to 1 strong, leaning the streaks
        float seconds = 0; ///< Real time, for the fall
        float light = 1;   ///< How lit the air is, 0 dark to 1 full day
    };

    /// Loads the rain texture from the install. Without it, nothing is drawn.
    void load_park_rain(render::Device& device, ParkRain& rain, const config::GameInstall& install);
    /// Draws the streaks into the open pass, over the opaque world.
    void draw_park_rain(render::Device& device, ParkRain& rain, const RainView& view);
    void destroy_park_rain(render::Device& device, ParkRain& rain);

} // namespace rct3::scene
