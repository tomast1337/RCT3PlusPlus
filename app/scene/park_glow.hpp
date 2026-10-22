// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glow round the bright parts of the park (the Bloom option). After the world is drawn and before the
// interface, the screen is copied and shrunk by halves into a chain of pictures, the first keeping only what is
// brighter than a threshold. Each picture is then grown back onto the next larger one, so small bright points
// spread wide and soft, and the largest is added over the screen.
//
// The light shafts (the Light shafts option) read the depth of the same copy: at half size, each pixel steps
// through the air between the eye and what it shows, lit where the sun's shadow map says the sun reaches, so the
// shadows of trees, rides and buildings cut dark gaps in the sunlit haze (app/shaders/shafts.slang). The result
// is added over the screen.
#pragma once

#include "render/device.hpp"

#include <array>
#include <cstddef>
#include <glm/mat4x4.hpp>

namespace rct3::scene
{

    struct ParkGlow
    {
        static constexpr std::size_t kLevels = 6; ///< The most halvings; fewer on a small screen

        render::PipelineHandle shrink; ///< Down the chain, replacing
        render::PipelineHandle add;    ///< Up the chain and onto the screen, adding
        render::PipelineHandle march;  ///< The light shafts
        render::TextureHandle screen;  ///< The world as drawn
        render::Extent screen_size;
        std::array<render::TextureHandle, kLevels> levels { }; ///< Half size, then each half the one before
        std::array<render::Extent, kLevels> level_sizes { };
        std::size_t used = 0;         ///< How many levels the screen's size allows
        render::TextureHandle shafts; ///< Half size
        render::Extent shafts_size;
    };

    struct LightShafts
    {
        glm::mat4 from_clip { 1.0f }; ///< The screen's clip space to the world
        render::SceneLight light;     ///< The sun, and its shadow, without which none are drawn
        float reach = 0.0f;           ///< How far into the air each pixel reads, in world units
        float strength = 0.0f;        ///< 0 draws none
    };

    /// Glows the screen as drawn so far, with the bloom, the light shafts or both. Call between passes of the
    /// screen; leaves a screen pass open.
    void draw_park_glow(render::Device& device, ParkGlow& glow, bool bloom, const LightShafts& shafts);
    void destroy_park_glow(render::Device& device, ParkGlow& glow);

} // namespace rct3::scene
