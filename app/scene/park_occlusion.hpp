// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ambient occlusion (our Ambient occlusion option): creases, corners and the ground under things darkened
// where less of the sky reaches them. After the world is drawn and before the glow, the screen's depth is
// copied and, from it alone, each pixel's openness traced (ground truth ambient occlusion,
// render::OcclusionStep). Few directions are traced a pixel, turned from pixel to pixel and frame to frame;
// a blur over 4 by 4 pixels and a blend with the frames before, followed to where each surface was then,
// make up the rest. The result darkens the screen.
#pragma once

#include "render/device.hpp"

#include <array>
#include <cstdint>

namespace rct3::scene
{

    struct ParkOcclusion
    {
        render::PipelineHandle step;                  ///< Trace, blur and blend
        render::PipelineHandle apply;                 ///< Onto the screen
        std::array<render::TextureHandle, 2> screens; ///< This frame's and the last's world, for their depth
        render::TextureHandle traced;
        render::TextureHandle blurred;
        std::array<render::TextureHandle, 2> results; ///< This frame's and the last's
        render::Extent size;
        std::uint32_t frame = 0;
        bool before = false;       ///< The last frame's result and screen hold the frame before; clear it when skipping one
        glm::mat4 last_view { 1 }; ///< The last frame's world to clip space
    };

    /// Darkens the screen as drawn so far, seen through `view_projection`, of which `projection` is the part
    /// after the eye. Call between passes of the screen; leaves a screen pass open.
    void draw_park_occlusion(render::Device& device, ParkOcclusion& occlusion, const glm::mat4& view_projection, const glm::mat4& projection);
    void destroy_park_occlusion(render::Device& device, ParkOcclusion& occlusion);

} // namespace rct3::scene
