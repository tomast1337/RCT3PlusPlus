// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_glow.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>

namespace rct3::scene
{

    namespace
    {
        // Linear brightness above which a pixel glows, and how strongly the glow is added back. Chosen by eye.
        constexpr float kThreshold = 0.72f;
        constexpr float kGain = 2.0f;
        constexpr float kStrength = 0.55f;
        // How far each spreading pass reads, in texels of the quarter-size picture.
        constexpr std::array kSpread { 0.5f, 1.5f, 2.5f, 3.5f };

        struct Corner
        {
            glm::vec2 position;
            glm::vec4 colour;
            glm::vec2 uv;
        };

        constexpr std::array kAttributes {
            render::VertexAttribute { 0, render::VertexFormat::Float2, offsetof(Corner, position) },
            render::VertexAttribute { 1, render::VertexFormat::Float4, offsetof(Corner, colour) },
            render::VertexAttribute { 2, render::VertexFormat::Float2, offsetof(Corner, uv) },
        };

        bool fit(render::Device& device, render::TextureHandle& texture, render::Extent& made, render::Extent size, std::string_view label, bool readable_depth = false)
        {
            if (texture && made.width == size.width && made.height == size.height)
                return true;
            if (texture)
                device.destroy_texture(texture);
            texture = { };
            render::TextureDesc desc;
            desc.width = size.width;
            desc.height = size.height;
            desc.target = true;
            // The screen is copied only into targets whose depth can be read.
            desc.readable_depth = readable_depth;
            desc.label = label;
            auto created = device.create_texture(desc);
            if (!created)
                return false;
            texture = *created;
            made = size;
            return true;
        }

        /// Draws `from` over the whole of the open pass through the Glow shader.
        void cover(render::Device& device, render::PipelineHandle pipeline, render::TextureHandle from, float reach, float threshold, float gain)
        {
            const glm::vec4 c { reach, threshold, gain, 1.0f };
            const std::array<Corner, 6> corners { {
                { { -1, -1 }, c, { 0, 0 } },
                { { 1, -1 }, c, { 1, 0 } },
                { { 1, 1 }, c, { 1, 1 } },
                { { -1, -1 }, c, { 0, 0 } },
                { { 1, 1 }, c, { 1, 1 } },
                { { -1, 1 }, c, { 0, 1 } },
            } };
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(corners)));
            if (!vertices)
                return;
            render::DrawCommand draw;
            draw.pipeline = pipeline;
            draw.vertices = *vertices;
            draw.count = static_cast<std::uint32_t>(corners.size());
            draw.texture = from;
            draw.filter = render::Filter::Linear;
            device.draw(draw);
        }

        bool ready(render::Device& device, ParkGlow& glow)
        {
            const render::VertexLayout layout { sizeof(Corner), kAttributes };
            for (auto [pipeline, blend, label] : { std::tuple { &glow.spread, render::BlendMode::Opaque, "glow spread" }, std::tuple { &glow.add, render::BlendMode::Add, "glow add" } })
            {
                if (*pipeline)
                    continue;
                auto made = device.create_pipeline({ .shader = render::Shader::Glow, .layout = layout, .blend = blend, .depth_test = false, .depth_write = false, .label = label });
                if (!made)
                    return false;
                *pipeline = *made;
            }
            const render::Extent screen = device.extent();
            const render::Extent small { std::max(screen.width / 4, 1u), std::max(screen.height / 4, 1u) };
            return fit(device, glow.screen, glow.screen_size, screen, "glow screen", true) && fit(device, glow.small[0], glow.small_size, small, "glow small") && fit(device, glow.small[1], glow.small_size, small, "glow small");
        }
    } // namespace

    void draw_park_glow(render::Device& device, ParkGlow& glow)
    {
        if (!ready(device, glow))
            return;
        device.end_pass();
        const bool copied = device.copy_screen(glow.screen);
        if (copied)
        {
            render::PassDesc into;
            into.target = glow.small[0];
            device.begin_pass(into);
            // Reads a texel out on each diagonal of the full screen: an average of four by four of its pixels.
            cover(device, glow.spread, glow.screen, 1.0f, kThreshold, kGain);
            device.end_pass();
            std::size_t at = 0;
            for (const float reach : kSpread)
            {
                into.target = glow.small[1 - at];
                device.begin_pass(into);
                cover(device, glow.spread, glow.small[at], reach, 0.0f, 1.0f);
                device.end_pass();
                at = 1 - at;
            }
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
            cover(device, glow.add, glow.small[at], 0.0f, 0.0f, kStrength);
            return;
        }
        render::PassDesc back;
        back.clear = false;
        device.begin_pass(back);
    }

    void destroy_park_glow(render::Device& device, ParkGlow& glow)
    {
        for (const render::PipelineHandle p : { glow.spread, glow.add })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const render::TextureHandle t : { glow.screen, glow.small[0], glow.small[1] })
        {
            if (t)
                device.destroy_texture(t);
        }
        glow = { };
    }

} // namespace rct3::scene
