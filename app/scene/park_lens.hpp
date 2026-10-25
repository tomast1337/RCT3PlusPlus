// SPDX-License-Identifier: GPL-3.0-or-later
//
// Water on the camera's lens (the Water droplets option, park/lens_drops.hpp). The lens gets soaked as the eye
// comes up out of water, splashed when the car ridden through a splash ride hits the water at the foot of a drop,
// and spotted by the rain while the view looks out into it. Each frame the water is pushed by down as the view
// sees it, the park's wind, the view turning and the eye flying forward. After the world and its glow are drawn,
// the screen is copied and each drop drawn over it through that copy (app/shaders/lens_drops.slang); with no
// water on the lens nothing is copied or drawn.
#pragma once

#include "park/lens_drops.hpp"
#include "render/device.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <optional>

namespace rct3::scene
{

    struct ParkLens
    {
        park::LensWater water;
        render::PipelineHandle pipeline;
        render::TextureHandle screen; ///< The world as drawn, seen through the drops
        render::Extent screen_size;
        bool was_under = false;              ///< The eye was under water a frame ago
        std::optional<glm::vec3> last_eye;   ///< A frame ago
        std::optional<glm::vec3> last_ahead; ///< The way the view looked a frame ago
        std::optional<float> last_rise;      ///< How fast the ridden car rose a frame ago, world units a second
        float rain_due = 0;                  ///< Drops the rain owes the lens, landed once whole
        float splash_wait = 0;               ///< Seconds before another splash can land
        float clock = 0;                     ///< Seconds, for the wind's slow turning
    };

    /// What the lens sees of the park this frame.
    struct LensSurroundings
    {
        glm::mat4 view_projection { 1.0f };
        glm::vec3 eye { };
        float aspect = 1;
        bool under_water = false;
        int rain = 0;   ///< 0 none, 1 light, 2 heavy, 3 a storm
        float wind = 0; ///< The park's wind, 0 calm to 1 strong
        /// How the car ridden moves, world units a second, while it rides through splashes; none otherwise.
        std::optional<glm::vec3> splashing_car;
    };

    /// Wets the lens as `around` says and moves its water on by `seconds`; all water goes when `on` is false.
    void advance_park_lens(ParkLens& lens, const LensSurroundings& around, bool on, float seconds);
    void destroy_park_lens(render::Device& device, ParkLens& lens);

} // namespace rct3::scene
