// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glow round the bright parts of the park (the Bloom option). After the world is drawn and before the
// interface, the screen is copied and shrunk by halves into a chain of pictures, the first keeping only what is
// brighter than a threshold. Each picture is then grown back onto the next larger one, so small bright points
// spread wide and soft, and the largest is added over the screen.
#pragma once

#include "render/device.hpp"

#include <array>
#include <cstddef>

namespace rct3::scene
{

    struct ParkGlow
    {
        static constexpr std::size_t kLevels = 6; ///< The most halvings; fewer on a small screen

        render::PipelineHandle shrink; ///< Down the chain, replacing
        render::PipelineHandle add;    ///< Up the chain and onto the screen, adding
        render::TextureHandle screen;  ///< The world as drawn
        render::Extent screen_size;
        std::array<render::TextureHandle, kLevels> levels { }; ///< Half size, then each half the one before
        std::array<render::Extent, kLevels> level_sizes { };
        std::size_t used = 0; ///< How many levels the screen's size allows
    };

    /// Glows the screen as drawn so far. Call between passes of the screen; leaves a screen pass open.
    void draw_park_glow(render::Device& device, ParkGlow& glow);
    void destroy_park_glow(render::Device& device, ParkGlow& glow);

} // namespace rct3::scene
