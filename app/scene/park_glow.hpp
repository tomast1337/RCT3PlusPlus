// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glow round the bright parts of the park (the Bloom option). After the world is drawn and before the
// interface, the screen is copied; what is brighter than a threshold is kept in a quarter-size picture, spread
// out by a few passes that each average four reads further apart, and added back over the screen.
#pragma once

#include "render/device.hpp"

#include <array>

namespace rct3::scene
{

    struct ParkGlow
    {
        render::PipelineHandle spread; ///< Into the small pictures
        render::PipelineHandle add;    ///< Back onto the screen
        render::TextureHandle screen;  ///< The world as drawn
        render::Extent screen_size;
        std::array<render::TextureHandle, 2> small; ///< Quarter size, drawn into in turn
        render::Extent small_size;
    };

    /// Glows the screen as drawn so far. Call between passes of the screen; leaves a screen pass open.
    void draw_park_glow(render::Device& device, ParkGlow& glow);
    void destroy_park_glow(render::Device& device, ParkGlow& glow);

} // namespace rct3::scene
