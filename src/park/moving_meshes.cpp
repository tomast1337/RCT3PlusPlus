// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/moving_meshes.hpp"

#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace rct3::park
{

    MovingMeshes moving_meshes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        // Each object alone at the origin.
        std::vector<ObjectMesh> alone;
        alone.reserve(objects.size());
        for (const save::PlacedObject& o : objects)
            alone.push_back(object_mesh(std::span(&o, 1), models, grid, { }, { }, { }, { { o.id, glm::mat4(1.0f) } }));
        return gathered_meshes(alone);
    }

    std::pair<MovingMeshes, std::vector<std::int32_t>> boned_meshes(const save::PlacedObject& object, const ObjectModel& model, const save::TerrainGrid& grid)
    {
        std::map<std::int32_t, ModelLevel> by_bone;
        if (model.rig_level < model.levels.size())
            for (const ModelPart& part : model.levels[model.rig_level].parts)
                if (part.bone >= 0)
                    by_bone[part.bone].parts.push_back(part);
        std::vector<ObjectMesh> alone;
        std::vector<std::int32_t> bones;
        ObjectModels one;
        for (auto& [bone, level] : by_bone)
        {
            ObjectModel piece;
            piece.positioning = model.positioning;
            piece.levels = { std::move(level) };
            one[model_key(object)] = std::move(piece);
            alone.push_back(object_mesh(std::span(&object, 1), one, grid, { }, { }, { }, { { object.id, glm::mat4(1.0f) } }));
            bones.push_back(bone);
        }
        return { gathered_meshes(alone), std::move(bones) };
    }

    MovingMeshes gathered_meshes(std::span<const ObjectMesh> alone)
    {
        MovingMeshes out;
        for (const ObjectMesh& one : alone)
        {
            out.own.insert(out.own.end(), one.vertices.begin(), one.vertices.end());
            out.ends.push_back(static_cast<std::uint32_t>(out.own.size()));
            out.mesh.placed += one.placed;
            out.mesh.missing += one.missing;
        }
        std::map<std::tuple<std::string, PartBlend, bool>, std::size_t> batch_of;
        std::vector<std::vector<std::uint32_t>> indices;
        for (std::size_t i = 0; i < alone.size(); ++i)
        {
            const std::uint32_t base = i == 0 ? 0 : out.ends[i - 1];
            for (const ObjectBatch& batch : alone[i].batches)
            {
                const auto [at, added] = batch_of.try_emplace({ batch.texture, batch.blend, batch.one_sided }, out.mesh.batches.size());
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
        out.places.assign(alone.size(), glm::mat4(1.0f));
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
            const glm::vec4* c = &m[0]; // The four columns
            for (std::uint32_t v = first; v < meshes.ends[i]; ++v)
            {
                const glm::vec3& p = meshes.own[v].position;
                const glm::vec3& n = meshes.own[v].normal;
                glm::vec3& to_p = meshes.mesh.vertices[v].position;
                glm::vec3& to_n = meshes.mesh.vertices[v].normal;
                to_p.x = c[0].x * p.x + c[1].x * p.y + c[2].x * p.z + c[3].x;
                to_p.y = c[0].y * p.x + c[1].y * p.y + c[2].y * p.z + c[3].y;
                to_p.z = c[0].z * p.x + c[1].z * p.y + c[2].z * p.z + c[3].z;
                to_n.x = c[0].x * n.x + c[1].x * n.y + c[2].x * n.z;
                to_n.y = c[0].y * n.x + c[1].y * n.y + c[2].y * n.z;
                to_n.z = c[0].z * n.x + c[1].z * n.y + c[2].z * n.z;
            }
            first = meshes.ends[i];
        }
    }

} // namespace rct3::park
