// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_lens.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

namespace rct3::scene
{

    namespace
    {
        // Chosen by eye.
        constexpr float kRainPerSecond[] = { 0.0f, 2.5f, 8.0f, 18.0f }; ///< Drops landing, by how hard it rains
        constexpr float kWindPush = 0.9f;                               ///< Screen heights a second the strongest wind pushes drops across
        constexpr float kWindSwing = 0.6f;                              ///< Radians the wind's way swings either side of its middle
        constexpr float kWindPeriod = 240.0f;                           ///< Seconds the wind takes to swing round and back
        constexpr float kSplashFall = 4.0f;                             ///< World units a second the car must be falling, to splash
        constexpr float kSplashPull = 25.0f;                            ///< And how sharply its fall must stop, world units a second squared
        constexpr float kSplashRest = 1.5f;                             ///< Seconds between splashes
        constexpr std::size_t kSplashDrops = 40;
        constexpr std::size_t kSplashSheets = 2;

        /// The point `clip` (x right, y up, both -1..1, at depth `depth`) stands for in the world.
        glm::vec3 unprojected(const glm::mat4& back, glm::vec2 clip, float depth)
        {
            const glm::vec4 p = back * glm::vec4(clip, depth, 1.0f);
            return glm::vec3(p) / p.w;
        }
    } // namespace

    void advance_park_lens(ParkLens& lens, const LensSurroundings& around, bool on, float seconds)
    {
        park::LensWater& water = lens.water;
        if (!on)
        {
            water.drops.clear();
            lens.was_under = around.under_water;
            lens.last_eye.reset();
            lens.last_ahead.reset();
            lens.last_rise.reset();
            return;
        }
        lens.clock += seconds;
        lens.splash_wait = std::max(0.0f, lens.splash_wait - seconds);

        // The view's own ways in the world, from the screen's middle and edges.
        const glm::mat4 back = glm::inverse(around.view_projection);
        const glm::vec3 middle = unprojected(back, { 0, 0 }, 0.5f);
        const glm::vec3 right = glm::normalize(unprojected(back, { 1, 0 }, 0.5f) - unprojected(back, { -1, 0 }, 0.5f));
        const glm::vec3 up = glm::normalize(unprojected(back, { 0, 1 }, 0.5f) - unprojected(back, { 0, -1 }, 0.5f));
        const glm::vec3 ahead = glm::normalize(middle - around.eye);
        const auto across = [&](glm::vec3 v) { return glm::vec2 { glm::dot(v, right), -glm::dot(v, up) }; };

        if (around.under_water)
            water.drops.clear();
        else if (lens.was_under)
            park::wet_lens(water, park::kSoakingDrops, park::kSoakingSheets, around.aspect);
        lens.was_under = around.under_water;

        // Rain lands on a lens looking out level or up into it, little on one looking down.
        const int rain = std::clamp(around.rain, 0, 3);
        if (!around.under_water && rain > 0)
        {
            lens.rain_due += kRainPerSecond[rain] * std::clamp(0.4f + ahead.y * 2.0f, 0.0f, 1.0f) * seconds;
            const auto whole = static_cast<std::size_t>(lens.rain_due);
            park::wet_lens(water, whole, 0, around.aspect);
            lens.rain_due -= static_cast<float>(whole);
        }

        // A splash: the car ridden was falling fast and the water stopped it short.
        if (around.splashing_car && seconds > 0)
        {
            const float rise = around.splashing_car->y;
            if (lens.last_rise && *lens.last_rise < -kSplashFall && (rise - *lens.last_rise) / seconds > kSplashPull && lens.splash_wait <= 0 && !around.under_water)
            {
                park::wet_lens(water, kSplashDrops, kSplashSheets, around.aspect);
                lens.splash_wait = kSplashRest;
            }
            lens.last_rise = rise;
        }
        else
            lens.last_rise.reset();

        // The wind keeps to one way, swinging slowly either side of it.
        const float way = kWindSwing * std::sin(lens.clock * 6.2831853f / kWindPeriod);
        const glm::vec3 wind = glm::vec3 { std::cos(way), 0.0f, std::sin(way) } * (std::clamp(around.wind, 0.0f, 1.0f) * kWindPush);
        park::LensPush push { .aspect = around.aspect, .down = across({ 0, -1, 0 }), .wind = across(wind) };
        if (lens.last_ahead && lens.last_eye && seconds > 0)
        {
            // Where the way the view looked a frame ago is on screen now: how far the world swept across it.
            const glm::vec4 was = around.view_projection * glm::vec4(around.eye + *lens.last_ahead * 1000.0f, 1.0f);
            if (was.w > 0)
                push.turn = glm::vec2 { was.x / was.w * around.aspect * 0.5f, -was.y / was.w * 0.5f } / seconds;
            push.onrush = glm::dot(around.eye - *lens.last_eye, ahead) / seconds;
        }
        lens.last_ahead = ahead;
        lens.last_eye = around.eye;
        if (!water.drops.empty())
            park::run_lens_water(water, push, seconds);
    }

    void destroy_park_lens(render::Device& device, ParkLens& lens)
    {
        if (lens.pipeline)
            device.destroy_pipeline(lens.pipeline);
        if (lens.screen)
            device.destroy_texture(lens.screen);
        lens = { };
    }

} // namespace rct3::scene
