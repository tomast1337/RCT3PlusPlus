// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/moving_meshes.hpp"

#include <map>
#include <string>
#include <utility>

namespace rct3::park
{

    MovingMeshes moving_meshes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        MovingMeshes out;
        // Each object alone at the origin, then its batches gathered with those of the others drawn alike.
        std::vector<ObjectMesh> alone;
        alone.reserve(objects.size());
        for (const save::PlacedObject& o : objects)
        {
            alone.push_back(object_mesh(std::span(&o, 1), models, grid, { }, { }, { }, { { o.id, glm::mat4(1.0f) } }));
            out.own.insert(out.own.end(), alone.back().vertices.begin(), alone.back().vertices.end());
            out.ends.push_back(static_cast<std::uint32_t>(out.own.size()));
            out.mesh.placed += alone.back().placed;
            out.mesh.missing += alone.back().missing;
        }
        std::map<std::pair<std::string, PartBlend>, std::size_t> batch_of;
        std::vector<std::vector<std::uint32_t>> indices;
        for (std::size_t i = 0; i < alone.size(); ++i)
        {
            const std::uint32_t base = i == 0 ? 0 : out.ends[i - 1];
            for (const ObjectBatch& batch : alone[i].batches)
            {
                const auto [at, added] = batch_of.try_emplace({ batch.texture, batch.blend }, out.mesh.batches.size());
                if (added)
                {
                    out.mesh.batches.push_back(batch);
                    indices.emplace_back();
                }
                for (std::uint32_t k = batch.first; k < batch.first + batch.count; ++k)
                    indices[at->second].push_back(base + alone[i].indices[k]);
            }
        }
        for (std::size_t b = 0; b < out.mesh.batches.size(); ++b)
        {
            out.mesh.batches[b].first = static_cast<std::uint32_t>(out.mesh.indices.size());
            out.mesh.batches[b].count = static_cast<std::uint32_t>(indices[b].size());
            out.mesh.indices.insert(out.mesh.indices.end(), indices[b].begin(), indices[b].end());
        }
        out.mesh.vertices = out.own;
        out.places.assign(objects.size(), glm::mat4(1.0f));
        return out;
    }

    void move_meshes(MovingMeshes& meshes, std::span<const glm::mat4> places)
    {
        std::uint32_t first = 0;
        for (std::size_t i = 0; i < meshes.ends.size() && i < places.size(); ++i)
        {
            const glm::mat4& m = places[i];
            if (meshes.places[i] == m)
            {
                first = meshes.ends[i];
                continue;
            }
            meshes.places[i] = m;
            // Plain arithmetic: thousands of vertices a frame, and vector types cost calls in unoptimised builds.
            const float* t = &m[0][0];
            for (std::uint32_t v = first; v < meshes.ends[i]; ++v)
            {
                const float* p = &meshes.own[v].position.x;
                const float* n = &meshes.own[v].normal.x;
                float* to_p = &meshes.mesh.vertices[v].position.x;
                float* to_n = &meshes.mesh.vertices[v].normal.x;
                for (int r = 0; r < 3; ++r)
                {
                    to_p[r] = t[r] * p[0] + t[4 + r] * p[1] + t[8 + r] * p[2] + t[12 + r];
                    to_n[r] = t[r] * n[0] + t[4 + r] * n[1] + t[8 + r] * n[2];
                }
            }
            first = meshes.ends[i];
        }
    }

} // namespace rct3::park
