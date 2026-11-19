// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky/mesh.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::park::sky
{

    namespace
    {

        /// How much the clouds darken at night.
        constexpr f32 kNightClouds = 0.7f;

        f32 farthest(const SkyShape& shape)
        {
            f32 out = 0;
            for (const object::ModelVertex& v : shape.vertices)
                out = std::max(out, glm::length(v.position));
            return out;
        }

        /// Keeps a drifting offset small, as the picture repeats every whole unit.
        f32 wrapped(f32 offset)
        {
            return offset - std::floor(offset);
        }

        void add(object::ObjectMesh& mesh, const SkyShape& shape, object::PartBlend blend, glm::vec3 eye, f32 scale, glm::vec4 colour, auto&& uv_of)
        {
            if (shape.vertices.empty() || shape.indices.empty() || colour.a <= 0)
                return;
            const auto base = static_cast<u32>(mesh.vertices.size());
            for (const object::ModelVertex& v : shape.vertices)
                mesh.vertices.push_back({ eye + v.position * scale, colour, uv_of(v.uv) });
            object::ObjectBatch batch;
            batch.texture = shape.texture;
            batch.picture = shape.texture;
            batch.blend = blend;
            batch.first = static_cast<u32>(mesh.indices.size());
            for (const u32 i : shape.indices)
            {
                if (i < shape.vertices.size())
                    mesh.indices.push_back(base + i);
            }
            batch.count = static_cast<u32>(mesh.indices.size()) - batch.first;
            mesh.batches.push_back(std::move(batch));
        }

    } // namespace

    object::ObjectMesh sky_mesh(const SkyModels& sky, const SkyView& view)
    {
        object::ObjectMesh mesh;
        f32 reach = 0;
        for (const SkyShape* shape : { &sky.dome, &sky.stars, &sky.milky_way, &sky.clouds })
            reach = std::max(reach, farthest(*shape));
        if (reach <= 0)
            return mesh;
        // One scale for all, so the domes keep their sizes against each other.
        const f32 scale = view.radius / reach;
        const f32 night = std::clamp(view.night, 0.0f, 1.0f);

        const f32 overcast = std::clamp(view.overcast, 0.0f, 1.0f);
        add(mesh, sky.dome, object::PartBlend::Opaque, view.eye, scale, glm::vec4(view.tint, 1.0f), [&](glm::vec2 uv) { return glm::vec2(view.day, uv.y); });
        const glm::vec4 fading { view.tint, std::clamp(view.fade, 0.0f, 1.0f) };
        add(mesh, sky.dome, object::PartBlend::Blended, view.eye, scale, fading, [&](glm::vec2 uv) { return glm::vec2(view.faded_day, uv.y); });
        const glm::vec4 starlight { 1.0f, 1.0f, 1.0f, night * (1 - overcast) };
        const auto same = [](glm::vec2 uv) { return uv; };
        add(mesh, sky.stars, object::PartBlend::Blended, view.eye, scale, starlight, same);
        add(mesh, sky.milky_way, object::PartBlend::Blended, view.eye, scale, starlight, same);

        // Rain clouds are darker than fair ones, and thicker. Chosen by eye.
        constexpr f32 kRainCloudShade = 0.45f;
        const f32 shade = (1 - kNightClouds * night) * (1 - kRainCloudShade * overcast);
        const glm::vec4 cloud { view.tint * shade, glm::mix(kCloudCover, 1.0f, overcast) };
        const f32 low = wrapped(view.time.value * kCloudDrift);
        const f32 high = wrapped(view.time.value * kHighCloudDrift);
        add(mesh, sky.clouds, object::PartBlend::Blended, view.eye, scale, cloud, [&](glm::vec2 uv) { return uv + glm::vec2(low, 0.0f); });
        add(mesh, sky.clouds, object::PartBlend::Blended, view.eye, scale, cloud, [&](glm::vec2 uv) { return uv * 2.0f + glm::vec2(high, high * 0.5f); });
        mesh.drawn = mesh.batches.size();
        return mesh;
    }

} // namespace rct3::park::sky