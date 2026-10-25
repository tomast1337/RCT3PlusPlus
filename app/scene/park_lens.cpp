// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_lens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <span>

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
        constexpr float kSplashRun = 3.0f;                              ///< World units a second the car must run on at
        constexpr float kSplashRest = 1.5f;                             ///< Seconds between splashes
        constexpr std::size_t kSplashDrops = 40;
        constexpr std::size_t kSplashSheets = 2;

        constexpr float kStretch = 2.5f; ///< A drop's length along its slide grows by this per screen height a second
        constexpr float kFadeIn = 0.15f; ///< Seconds a drop takes to show in full
        constexpr float kFadeOut = 1.0f; ///< And to dry away at the end of its life

        constexpr std::array kAttributes {
            render::VertexAttribute { 0, render::VertexFormat::Float2, offsetof(LensCorner, position) },
            render::VertexAttribute { 1, render::VertexFormat::Float4, offsetof(LensCorner, colour) },
            render::VertexAttribute { 2, render::VertexFormat::Float2, offsetof(LensCorner, uv) },
        };

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

        // Rain lands on a lens looking out level or up into it, less the more it looks down, none straight down.
        const int rain = std::clamp(around.rain, 0, 3);
        if (!around.under_water && rain > 0)
        {
            lens.rain_due += kRainPerSecond[rain] * std::clamp(1.0f + ahead.y * 1.5f, 0.0f, 1.0f) * seconds;
            const auto whole = static_cast<std::size_t>(lens.rain_due);
            park::wet_lens(water, whole, 0, around.aspect);
            lens.rain_due -= static_cast<float>(whole);
        }

        // A splash: the car ridden was falling fast and the water stopped it short.
        if (around.splashing_car && seconds > 0)
        {
            const float rise = around.splashing_car->y;
            // It runs on through the water; a car held at the top of a spike stopped short too, but stands still.
            // shortcut: any sharp stop of a fast fall on a splash ride splashes, water under it or not; check the track
            // piece's water once pieces say so.
            const bool running_on = glm::length(glm::vec2 { around.splashing_car->x, around.splashing_car->z }) > kSplashRun;
            if (lens.last_rise && *lens.last_rise < -kSplashFall && (rise - *lens.last_rise) / seconds > kSplashPull && running_on && lens.splash_wait <= 0 && !around.under_water)
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

    void draw_park_lens(render::Device& device, ParkLens& lens)
    {
        if (lens.water.drops.empty())
            return;
        if (!lens.pipeline)
        {
            auto made = device.create_pipeline({ .shader = render::Shader::LensDrops, .layout = { sizeof(LensCorner), kAttributes }, .blend = render::BlendMode::Alpha, .depth_test = false, .depth_write = false, .label = "lens drops" });
            if (!made)
                return;
            lens.pipeline = *made;
        }
        const render::Extent size = device.extent();
        if (!lens.screen || lens.screen_size.width != size.width || lens.screen_size.height != size.height)
        {
            if (lens.screen)
                device.destroy_texture(lens.screen);
            lens.screen = { };
            // The screen is copied only into targets whose depth can be read.
            auto created = device.create_texture({ .width = size.width, .height = size.height, .label = "lens screen", .target = true, .readable_depth = true });
            if (!created)
                return;
            lens.screen = *created;
            lens.screen_size = size;
        }
        const float aspect = static_cast<float>(size.width) / static_cast<float>(std::max(size.height, 1u));
        std::vector<LensCorner>& corners = lens.corners;
        corners.clear();
        for (const park::LensDrop& drop : lens.water.drops)
        {
            const float shown = std::min({ 1.0f, drop.age / kFadeIn, (drop.life - drop.age) / kFadeOut });
            if (shown <= 0)
                continue;
            // Long along its slide, a little narrower across it; to screen shares, then clip space with y up.
            const float speed = glm::length(drop.velocity);
            const glm::vec2 along = speed > 1e-4f ? drop.velocity / speed : glm::vec2 { 0, 1 };
            const glm::vec2 side { -along.y, along.x };
            const glm::vec2 a = along * drop.radius * (1 + speed * kStretch);
            const glm::vec2 b = side * drop.radius / std::sqrt(1 + speed * kStretch);
            const glm::vec4 colour { shown, drop.sheet ? 1.0f : 0.0f, drop.at.x / aspect, drop.at.y };
            const auto clip = [&](glm::vec2 p) { return glm::vec2 { p.x / aspect * 2 - 1, 1 - p.y * 2 }; };
            const LensCorner c00 { clip(drop.at - a - b), colour, { -1, -1 } };
            const LensCorner c10 { clip(drop.at - a + b), colour, { 1, -1 } };
            const LensCorner c11 { clip(drop.at + a + b), colour, { 1, 1 } };
            const LensCorner c01 { clip(drop.at + a - b), colour, { -1, 1 } };
            corners.insert(corners.end(), { c00, c10, c11, c00, c11, c01 });
        }
        if (corners.empty())
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(corners)));
        if (!vertices)
            return;
        device.end_pass();
        const bool copied = device.copy_screen(lens.screen);
        render::PassDesc back;
        back.clear = false;
        device.begin_pass(back);
        if (!copied)
            return;
        render::DrawCommand draw;
        draw.pipeline = lens.pipeline;
        draw.vertices = *vertices;
        draw.count = static_cast<std::uint32_t>(corners.size());
        draw.texture = lens.screen;
        draw.filter = render::Filter::Linear;
        device.draw(draw);
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
