// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/waterfalls.hpp"

#include "logging/log.hpp"
#include "park/camera.hpp"

namespace rct3::scene::park
{

    namespace
    {

        constexpr glm::vec4 kNoLake { 0.2f, 0.45f, 0.55f, 0.5f }; ///< Chosen by eye: a fall's colour where neither of its lakes is found

        void drop_buffers(render::Device& device, ParkWaterfalls& falls)
        {
            for (render::BufferHandle* b : { &falls.vertices, &falls.indices })
            {
                if (*b)
                    device.destroy_buffer(*b);
                *b = { };
            }
        }

    } // namespace

    void create_park_waterfalls(render::Device& device, ParkWaterfalls& falls, const render::VertexLayout& layout)
    {
        if (auto made = device.create_pipeline({ .shader = render::Shader::Waterfall, .layout = layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "waterfalls" }))
            falls.pipeline = *made;
        else
            logging::logger("park").warning("waterfalls are not drawn: {}", made.error());
    }

    void refresh_park_waterfalls(render::Device& device, ParkWaterfalls& falls, std::span<const save::model::Waterfall> park_falls, const rct3::park::water::WaterSurface& lakes)
    {
        falls.lakes.clear();
        for (const save::model::Waterfall& fall : park_falls)
        {
            // Just under each lake's level, where its water is.
            const auto lake = [&](const save::model::WorldPoint3& p) { return rct3::park::water::water_above(lakes, { p.x, p.y - 0.1f, p.z }); };
            const auto upper = lake(fall.top);
            const auto lower = lake(fall.bottom);
            const glm::vec4 either = upper ? upper->colour : (lower ? lower->colour : kNoLake);
            falls.lakes.push_back({ .upper = upper ? upper->colour : either, .lower = lower ? lower->colour : either });
        }
        rct3::park::waterfall_mesh(park_falls, falls.lakes, falls.detail, falls.mesh);
        drop_buffers(device, falls);
        if (!falls.pipeline || falls.mesh.indices.empty())
            return;
        auto vertices = device.create_buffer({ .usage = render::BufferUsage::Vertex, .data = std::as_bytes(std::span(falls.mesh.vertices)), .label = "waterfalls" });
        auto indices = device.create_buffer({ .usage = render::BufferUsage::Index, .data = std::as_bytes(std::span(falls.mesh.indices)), .label = "waterfall indices" });
        if (vertices)
            falls.vertices = *vertices;
        if (indices)
            falls.indices = *indices;
        if (!vertices || !indices)
        {
            logging::logger("park").warning("waterfalls: {}", vertices ? indices.error() : vertices.error());
            drop_buffers(device, falls);
        }
    }

    void draw_park_waterfalls(render::Device& device, const ParkWaterfalls& falls, const ParkWater& water, const glm::mat4& transform, const render::SceneLight& light, glm::vec3 eye, float time, FallsSeen seen)
    {
        using rct3::park::FallPart;
        if (!falls.vertices || !falls.indices || !water.waves || (seen == FallsSeen::Mirrored && falls.detail < rct3::park::WaterfallDetail::High))
            return;
        const bool behind = seen == FallsSeen::WithScene && water.have_behind;
        for (std::size_t part = 0; part < falls.mesh.parts.size(); ++part)
        {
            const rct3::park::FallBatch batch = falls.mesh.parts[part];
            const bool sheet_only = seen == FallsSeen::Mirrored || seen == FallsSeen::FromUnderWater;
            if (batch.count == 0 || (sheet_only && static_cast<FallPart>(part) != FallPart::Sheet))
                continue;
            render::DrawCommand draw { .pipeline = falls.pipeline, .vertices = { falls.vertices }, .indices = { falls.indices }, .index_type = render::IndexType::U32, .count = batch.count, .first = batch.first, .transform = transform };
            draw.texture = water.waves;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            draw.light = light;
            if (behind)
                draw.extra_textures = { render::TextureHandle { }, water.behind, device.depth_texture(water.behind), render::TextureHandle { } };
            draw.water = { .eye = eye, .time = time, .near = rct3::park::kNear, .far = rct3::park::kFar, .refraction = behind, .fall_part = static_cast<int>(part), .fall_detail = static_cast<int>(falls.detail) };
            device.draw(draw);
        }
    }

    void destroy_park_waterfalls(render::Device& device, ParkWaterfalls& falls)
    {
        drop_buffers(device, falls);
        if (falls.pipeline)
            device.destroy_pipeline(falls.pipeline);
        falls = { };
    }

} // namespace rct3::scene::park
