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
        // Brightness above which a pixel glows, and how strongly the chain's average level is added back. Chosen by
        // eye.
        constexpr float kThreshold = 0.72f;
        constexpr float kStrength = 0.8f;
        // How far apart the growing tent reads, in texels of the smaller picture.
        constexpr float kReach = 1.0f;
        // The chain stops before a picture would be narrower than this.
        constexpr std::uint32_t kSmallest = 8;

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

        /// Draws `draw` (its pipeline, texture and the rest) over the whole of the open pass, every corner coloured
        /// `c`. Clip space Y points up and texture rows run down, so the top corners read the texture's first row.
        void cover(render::Device& device, render::DrawCommand draw, glm::vec4 c)
        {
            const std::array<Corner, 6> corners { {
                { { -1, -1 }, c, { 0, 1 } },
                { { 1, -1 }, c, { 1, 1 } },
                { { 1, 1 }, c, { 1, 0 } },
                { { -1, -1 }, c, { 0, 1 } },
                { { 1, 1 }, c, { 1, 0 } },
                { { -1, 1 }, c, { 0, 0 } },
            } };
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(corners)));
            if (!vertices)
                return;
            draw.vertices = *vertices;
            draw.count = static_cast<std::uint32_t>(corners.size());
            device.draw(draw);
        }

        /// Draws `from` over the whole of the open pass through the Glow shader, shrinking or growing it.
        void cover(render::Device& device, render::PipelineHandle pipeline, render::TextureHandle from, bool shrinking, float reach, float threshold, float gain)
        {
            render::DrawCommand draw;
            draw.pipeline = pipeline;
            draw.texture = from;
            draw.filter = render::Filter::Linear;
            cover(device, draw, { reach, threshold, gain, shrinking ? 1.0f : 0.0f });
        }

        bool ready(render::Device& device, ParkGlow& glow)
        {
            const render::VertexLayout layout { sizeof(Corner), kAttributes };
            for (auto [pipeline, shader, blend, label] : { std::tuple { &glow.shrink, render::Shader::Glow, render::BlendMode::Opaque, "glow shrink" }, std::tuple { &glow.add, render::Shader::Glow, render::BlendMode::Add, "glow add" }, std::tuple { &glow.march, render::Shader::Shafts, render::BlendMode::Opaque, "light shafts" } })
            {
                if (*pipeline)
                    continue;
                auto made = device.create_pipeline({ .shader = shader, .layout = layout, .blend = blend, .depth_test = false, .depth_write = false, .label = label });
                if (!made)
                    return false;
                *pipeline = *made;
            }
            const render::Extent screen = device.extent();
            if (!fit(device, glow.screen, glow.screen_size, screen, "glow screen", true))
                return false;
            render::Extent size = screen;
            glow.used = 0;
            while (glow.used < ParkGlow::kLevels && std::min(size.width, size.height) / 2 >= kSmallest)
            {
                size = { size.width / 2, size.height / 2 };
                if (!fit(device, glow.levels[glow.used], glow.level_sizes[glow.used], size, "glow level"))
                    return false;
                ++glow.used;
            }
            return glow.used > 0;
        }
    } // namespace

    void draw_park_glow(render::Device& device, ParkGlow& glow, bool bloom, const LightShafts& shafts)
    {
        const bool marched = shafts.strength > 0.0f && shafts.light.shadow_map && shafts.light.shadow_view;
        if (!ready(device, glow) || (marched && !fit(device, glow.shafts, glow.shafts_size, glow.level_sizes[0], "light shafts")))
            return;
        device.end_pass();
        const bool copied = device.copy_screen(glow.screen);
        if (copied && bloom)
        {
            render::PassDesc into;
            for (std::size_t i = 0; i < glow.used; ++i)
            {
                into.target = glow.levels[i];
                device.begin_pass(into);
                cover(device, glow.shrink, i == 0 ? glow.screen : glow.levels[i - 1], true, 0.0f, i == 0 ? kThreshold : 0.0f, 1.0f);
                device.end_pass();
            }
            // Each level grown onto the next larger keeps what that one holds, so the first ends with them all.
            into.clear = false;
            for (std::size_t i = glow.used - 1; i > 0; --i)
            {
                into.target = glow.levels[i - 1];
                device.begin_pass(into);
                cover(device, glow.add, glow.levels[i], false, kReach, 0.0f, 1.0f);
                device.end_pass();
            }
        }
        if (copied && marched)
        {
            render::PassDesc into;
            into.target = glow.shafts;
            device.begin_pass(into);
            render::DrawCommand draw;
            draw.pipeline = glow.march;
            draw.transform = shafts.from_clip;
            draw.light = shafts.light;
            draw.texture = glow.screen;
            draw.extra_textures = { device.depth_texture(glow.screen), shafts.light.shadow_map, shafts.light.shadow_view };
            cover(device, draw, { shafts.strength, shafts.reach, 0.0f, 0.0f });
            device.end_pass();
        }
        render::PassDesc back;
        back.clear = false;
        device.begin_pass(back);
        if (copied && bloom)
            cover(device, glow.add, glow.levels[0], false, kReach, 0.0f, kStrength / static_cast<float>(glow.used));
        if (copied && marched)
            cover(device, glow.add, glow.shafts, false, kReach, 0.0f, 1.0f);
    }

    void destroy_park_glow(render::Device& device, ParkGlow& glow)
    {
        for (const render::PipelineHandle p : { glow.shrink, glow.add, glow.march })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const render::TextureHandle t : { glow.screen, glow.shafts })
        {
            if (t)
                device.destroy_texture(t);
        }
        for (const render::TextureHandle t : glow.levels)
        {
            if (t)
                device.destroy_texture(t);
        }
        glow = { };
    }

} // namespace rct3::scene
