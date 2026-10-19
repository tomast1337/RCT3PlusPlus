// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_occlusion.hpp"

#include <span>
#include <string_view>

namespace rct3::scene
{

    namespace
    {
        // Chosen by eye: how far round a point is looked at, in world units (metres); how dark
        // the darkest corners get; and the last frames' share of each result.
        constexpr float kRadius = 4.0f;
        constexpr float kStrength = 0.8f;
        // shortcut: the results are kept in 8 bits a channel, as targets are, so a share near 1 would leave the
        // blend stuck a step short of the new value; 0.85 keeps that under 0.03. Upgrade to a float target if
        // it shows.
        constexpr float kHistory = 0.85f;

        constexpr std::array kAttributes { render::VertexAttribute { 0, render::VertexFormat::Float2, 0 } };

        bool fit(render::Device& device, render::TextureHandle& texture, render::Extent size, std::string_view label, bool readable_depth = false)
        {
            if (texture)
                device.destroy_texture(texture);
            texture = { };
            render::TextureDesc desc;
            desc.width = size.width;
            desc.height = size.height;
            desc.target = true;
            desc.readable_depth = readable_depth;
            desc.label = label;
            auto created = device.create_texture(desc);
            if (!created)
                return false;
            texture = *created;
            return true;
        }

        bool ready(render::Device& device, ParkOcclusion& o)
        {
            const render::VertexLayout layout { sizeof(glm::vec2), kAttributes };
            if (!o.step)
            {
                auto made = device.create_pipeline({ .shader = render::Shader::Occlusion, .layout = layout, .depth_test = false, .depth_write = false, .label = "occlusion step" });
                if (!made)
                    return false;
                o.step = *made;
            }
            if (!o.apply)
            {
                auto made = device.create_pipeline({ .shader = render::Shader::Occlusion, .layout = layout, .blend = render::BlendMode::Alpha, .depth_test = false, .depth_write = false, .label = "occlusion apply" });
                if (!made)
                    return false;
                o.apply = *made;
            }
            const render::Extent size = device.extent();
            if (o.traced && o.size.width == size.width && o.size.height == size.height)
                return true;
            o.before = false;
            o.size = size;
            return fit(device, o.screens[0], size, "occlusion screen", true) && fit(device, o.screens[1], size, "occlusion screen", true) && fit(device, o.traced, size, "occlusion traced") && fit(device, o.blurred, size, "occlusion blurred") && fit(device, o.results[0], size, "occlusion result") && fit(device, o.results[1], size, "occlusion result");
        }

        /// One step over the whole of a pass into `into`, or over the screen without a target.
        void cover(render::Device& device, render::PipelineHandle pipeline, render::TextureHandle into, const render::OcclusionStep& step, const glm::mat4& matrix, render::TextureHandle texture, std::array<render::TextureHandle, 3> extras = { })
        {
            render::PassDesc pass;
            pass.target = into;
            pass.clear = into ? true : false;
            device.begin_pass(pass);
            static constexpr std::array<glm::vec2, 6> kCorners { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } } };
            if (const auto vertices = device.allocate_transient(std::as_bytes(std::span(kCorners))))
            {
                render::DrawCommand draw;
                draw.pipeline = pipeline;
                draw.vertices = *vertices;
                draw.count = static_cast<std::uint32_t>(kCorners.size());
                draw.transform = matrix;
                draw.texture = texture;
                draw.extra_textures = { extras[0], extras[1], extras[2] };
                draw.occlusion = step;
                device.draw(draw);
            }
            if (into)
                device.end_pass();
        }
    } // namespace

    void draw_park_occlusion(render::Device& device, ParkOcclusion& o, const glm::mat4& view_projection, const glm::mat4& projection)
    {
        render::PassDesc back;
        back.clear = false;
        if (!ready(device, o))
            return;
        device.end_pass();
        const std::size_t now = o.frame & 1;
        const std::size_t then = 1 - now;
        if (!device.copy_screen(o.screens[now]))
        {
            device.begin_pass(back);
            return;
        }
        const render::TextureHandle depth = device.depth_texture(o.screens[now]);
        render::OcclusionStep step;
        step.frame = o.frame;
        step.radius = kRadius;
        step.strength = kStrength;
        // The projection gives stored = a + b / depth (render::OcclusionStep::depth_terms).
        step.depth_terms = { projection[2][2], projection[3][2] };
        step.kind = render::OcclusionStep::Kind::Trace;
        cover(device, o.step, o.traced, step, glm::inverse(projection), depth);
        step.kind = render::OcclusionStep::Kind::Blur;
        cover(device, o.step, o.blurred, step, { }, o.traced, { depth });
        step.kind = render::OcclusionStep::Kind::Blend;
        step.history = o.before ? kHistory : 0.0f;
        cover(device, o.step, o.results[now], step, o.last_view * glm::inverse(view_projection), o.blurred, { o.results[then], depth, device.depth_texture(o.screens[then]) });
        step.kind = render::OcclusionStep::Kind::Apply;
        cover(device, o.apply, { }, step, { }, o.results[now]);
        o.last_view = view_projection;
        o.before = true;
        ++o.frame;
    }

    void destroy_park_occlusion(render::Device& device, ParkOcclusion& o)
    {
        for (const render::PipelineHandle p : { o.step, o.apply })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const render::TextureHandle t : { o.screens[0], o.screens[1], o.traced, o.blurred, o.results[0], o.results[1] })
        {
            if (t)
                device.destroy_texture(t);
        }
        o = { };
    }

} // namespace rct3::scene
