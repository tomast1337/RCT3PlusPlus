// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_rain.hpp"

#include "logging/log.hpp"
#include "park/water_flow.hpp"
#include "random/generator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>
#include <vector>

namespace rct3::scene
{

    namespace
    {
        constexpr std::string_view kRainFile = "Rain/Rain.common.ovl";
        constexpr std::string_view kRainTexture = "Rain";

        // Chosen by eye.
        constexpr std::size_t kMostDrops = 2400; ///< Streaks in a downpour
        constexpr float kAcross = 40;            ///< Metres the box of drops spans round the eye, each way
        constexpr float kHigh = 24;              ///< And up and down
        constexpr float kFall = 11;              ///< Metres a second a drop falls
        constexpr float kLean = 6;               ///< Metres a second the strongest wind carries it
        constexpr float kLength = 0.9f;          ///< Of a streak, in metres
        constexpr float kWidth = 0.02f;          ///< Half a streak's width, near
        constexpr float kWidening = 0.12f;       ///< Share of its distance a streak widens by, so far ones still show
        constexpr float kNearest = 2.5f;         ///< Metres from the eye inside which drops are not drawn
        constexpr float kOpacity = 0.5f;         ///< Of the nearest streaks; farther ones fade out
        const glm::vec3 kWindWay = glm::normalize(glm::vec3(1.0f, 0.0f, 0.35f));

        using Corner = RainCorner;

        constexpr std::array kAttributes {
            render::VertexAttribute { 0, render::VertexFormat::Float3, offsetof(Corner, position) },
            render::VertexAttribute { 1, render::VertexFormat::Float4, offsetof(Corner, colour) },
            render::VertexAttribute { 2, render::VertexFormat::Float2, offsetof(Corner, uv) },
        };

        /// `value` wrapped into [0, span).
        float wrapped(float value, float span)
        {
            return value - span * std::floor(value / span);
        }

        /// A share between 0 and 1 from the drop's number, the same every frame.
        float share(std::uint64_t& state)
        {
            return static_cast<float>(random::split_mix(state) >> 40) / static_cast<float>(1ull << 24);
        }
    } // namespace

    void load_park_rain(render::Device& device, ParkRain& rain, const config::GameInstall& install)
    {
        const auto file = install.find(kRainFile);
        if (!file)
            return;
        const auto image = park::load_flow_picture(*file, kRainTexture);
        if (!image)
        {
            logging::logger("weather").warning("no rain: {}", image.error());
            return;
        }
        if (auto texture = device.create_texture({ .width = image->width, .height = image->height, .pixels = std::as_bytes(std::span(image->pixels)), .label = "rain" }))
            rain.streak = *texture;
        const render::VertexLayout layout { sizeof(Corner), kAttributes };
        if (auto made = device.create_pipeline({ .shader = render::Shader::Textured, .layout = layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "rain" }))
            rain.pipeline = *made;
    }

    void draw_park_rain(render::Device& device, ParkRain& rain, const RainView& view)
    {
        const auto count = static_cast<std::size_t>(std::clamp(view.amount, 0.0f, 1.0f) * static_cast<float>(kMostDrops));
        if (!rain.streak || !rain.pipeline || count == 0)
            return;
        const glm::vec3 velocity = kWindWay * (view.wind * kLean) + glm::vec3(0.0f, -kFall, 0.0f);
        const glm::vec3 along = glm::normalize(velocity);
        const glm::vec3 moved = velocity * view.seconds;
        const glm::vec3 corner = view.eye - glm::vec3(kAcross, kHigh, kAcross) / 2.0f;
        const float shade = std::clamp(view.light, 0.15f, 1.0f);
        std::vector<Corner>& corners = rain.corners;
        corners.clear();
        for (std::size_t i = 0; i < count; ++i)
        {
            std::uint64_t state = i;
            const glm::vec3 start { share(state) * kAcross, share(state) * kHigh, share(state) * kAcross };
            // Each drop keeps its place in the world, wrapped into the box about the eye.
            const glm::vec3 p = corner + glm::vec3(wrapped(start.x + moved.x - corner.x, kAcross), wrapped(start.y + moved.y - corner.y, kHigh), wrapped(start.z + moved.z - corner.z, kAcross));
            const glm::vec3 to_eye = view.eye - p;
            const float distance = glm::length(to_eye);
            if (distance < kNearest)
                continue;
            const glm::vec3 across = glm::cross(along, to_eye / distance);
            if (glm::dot(across, across) < 1e-6f)
                continue;
            const glm::vec3 side = glm::normalize(across) * (kWidth * (1 + kWidening * distance));
            const float fade = 1 - std::min(distance / (kAcross / 2), 1.0f);
            const glm::vec4 colour { glm::vec3(0.8f, 0.82f, 0.88f) * shade, kOpacity * fade };
            const glm::vec3 tail = p - along * kLength;
            const Corner a { tail - side, colour, { 0, 1 } };
            const Corner b { tail + side, colour, { 1, 1 } };
            const Corner c { p + side, colour, { 1, 0 } };
            const Corner d { p - side, colour, { 0, 0 } };
            corners.insert(corners.end(), { a, b, c, a, c, d });
        }
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(corners)));
        if (!vertices || corners.empty())
            return;
        render::DrawCommand draw;
        draw.pipeline = rain.pipeline;
        draw.vertices = *vertices;
        draw.count = static_cast<std::uint32_t>(corners.size());
        draw.transform = view.view_projection;
        draw.texture = rain.streak;
        draw.filter = render::Filter::Linear;
        device.draw(draw);
    }

    void destroy_park_rain(render::Device& device, ParkRain& rain)
    {
        if (rain.pipeline)
            device.destroy_pipeline(rain.pipeline);
        if (rain.streak)
            device.destroy_texture(rain.streak);
        rain = { };
    }

} // namespace rct3::scene
