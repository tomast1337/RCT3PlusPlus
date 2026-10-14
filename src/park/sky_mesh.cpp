// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::park
{

    namespace
    {

        /// How much the clouds darken at night.
        constexpr float kNightClouds = 0.7f;

        float farthest(const SkyShape& shape)
        {
            float out = 0;
            for (const ModelVertex& v : shape.vertices)
                out = std::max(out, glm::length(v.position));
            return out;
        }

        /// Keeps a drifting offset small, as the picture repeats every whole unit.
        float wrapped(float offset)
        {
            return offset - std::floor(offset);
        }

        void add(ObjectMesh& mesh, const SkyShape& shape, PartBlend blend, glm::vec3 eye, float scale, glm::vec4 colour, auto&& uv_of)
        {
            if (shape.vertices.empty() || shape.indices.empty() || colour.a <= 0)
                return;
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const ModelVertex& v : shape.vertices)
                mesh.vertices.push_back({ eye + v.position * scale, colour, uv_of(v.uv) });
            ObjectBatch batch;
            batch.texture = shape.texture;
            batch.picture = shape.texture;
            batch.blend = blend;
            batch.first = static_cast<std::uint32_t>(mesh.indices.size());
            for (const std::uint32_t i : shape.indices)
            {
                if (i < shape.vertices.size())
                    mesh.indices.push_back(base + i);
            }
            batch.count = static_cast<std::uint32_t>(mesh.indices.size()) - batch.first;
            mesh.batches.push_back(std::move(batch));
        }

    } // namespace

    ObjectMesh sky_mesh(const SkyModels& sky, const SkyView& view)
    {
        ObjectMesh mesh;
        float reach = 0;
        for (const SkyShape* shape : { &sky.dome, &sky.stars, &sky.milky_way, &sky.clouds })
            reach = std::max(reach, farthest(*shape));
        if (reach <= 0)
            return mesh;
        // One scale for all, so the domes keep their sizes against each other.
        const float scale = view.radius / reach;
        const float night = std::clamp(view.night, 0.0f, 1.0f);

        add(mesh, sky.dome, PartBlend::Opaque, view.eye, scale, glm::vec4(1.0f), [&](glm::vec2 uv) { return glm::vec2(view.day, uv.y); });
        const glm::vec4 starlight { 1.0f, 1.0f, 1.0f, night };
        const auto same = [](glm::vec2 uv) { return uv; };
        add(mesh, sky.stars, PartBlend::Blended, view.eye, scale, starlight, same);
        add(mesh, sky.milky_way, PartBlend::Blended, view.eye, scale, starlight, same);

        const float shade = 1 - kNightClouds * night;
        const glm::vec4 cloud { shade, shade, shade, kCloudCover };
        const float low = wrapped(view.time * kCloudDrift);
        const float high = wrapped(view.time * kHighCloudDrift);
        add(mesh, sky.clouds, PartBlend::Blended, view.eye, scale, cloud, [&](glm::vec2 uv) { return uv + glm::vec2(low, 0.0f); });
        add(mesh, sky.clouds, PartBlend::Blended, view.eye, scale, cloud, [&](glm::vec2 uv) { return uv * 2.0f + glm::vec2(high, high * 0.5f); });
        mesh.placed = mesh.batches.size();
        return mesh;
    }

} // namespace rct3::park
