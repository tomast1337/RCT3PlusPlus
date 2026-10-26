// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/moving_meshes.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iterator>
#include <map>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>

namespace rct3::park
{

    MovingMeshes moving_meshes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        // Each object alone at the origin, and the joints of those on arms after them.
        std::vector<ObjectMesh> alone;
        std::vector<ObjectMesh> joints;
        alone.reserve(objects.size());
        for (const save::PlacedObject& o : objects)
        {
            const auto model = models.find(model_ref(o));
            if (model == models.end() || model->second.arm_joints.empty())
            {
                alone.push_back(object_mesh(std::span(&o, 1), models, grid, { }, { }, { }, { { o.id, glm::mat4(1.0f) } }));
                continue;
            }
            for (std::int32_t joint = -1; joint < std::ssize(model->second.arm_joints); ++joint)
            {
                ObjectModels one;
                ObjectModel& piece = one[model->first] = model->second;
                for (ModelLevel& level : piece.levels)
                    std::erase_if(level.parts, [&](const ModelPart& part) { return part.bone != joint; });
                (joint < 0 ? alone : joints).push_back(object_mesh(std::span(&o, 1), one, grid, { }, { }, { }, { { o.id, glm::mat4(1.0f) } }));
            }
        }
        alone.insert(alone.end(), std::make_move_iterator(joints.begin()), std::make_move_iterator(joints.end()));
        return gathered_meshes(alone);
    }

    std::vector<glm::mat4> arm_moves(std::span<const glm::mat4> joints, std::span<const float> degrees)
    {
        std::vector<glm::mat4> moves;
        glm::mat4 carried(1.0f);
        for (std::size_t j = 0; j < joints.size(); ++j)
        {
            const float turn = j < degrees.size() ? glm::radians(degrees[j]) : 0.0f;
            // Turned in the joint's own space: taken there from the model's, turned, and put back.
            carried = carried * joints[j] * glm::rotate(glm::mat4(1.0f), turn, j == 0 ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)) * glm::inverse(joints[j]);
            moves.push_back(carried);
        }
        return moves;
    }

    std::pair<MovingMeshes, std::vector<std::int32_t>> boned_meshes(const save::PlacedObject& object, const ObjectModel& model, const save::TerrainGrid& grid)
    {
        std::vector<ObjectMesh> alone;
        std::vector<VertexSkin> skins;
        if (model.rig && model.rig_level < model.levels.size())
            for (const ModelPart& part : model.levels[model.rig_level].parts)
            {
                ObjectModels one;
                ObjectModel& piece = one[model_key(object)];
                piece.positioning = model.positioning;
                piece.levels = { ModelLevel { .distance = 0, .parts = { part }, .billboard = std::nullopt } };
                alone.push_back(object_mesh(std::span(&object, 1), one, grid, { }, { }, { }, { { object.id, glm::mat4(1.0f) } }));
                // One part makes one run, so the piece keeps the part's vertex order, unless it was left out.
                const std::size_t before = skins.size();
                for (const ModelVertex& v : part.vertices)
                    skins.push_back({ v.bones, v.weights });
                skins.resize(before + alone.back().vertices.size());
            }
        MovingMeshes out = gathered_meshes(alone);
        out.skins = std::move(skins);
        std::vector<std::int32_t> bones(model.rig ? model.rig->parents.size() : 0);
        std::iota(bones.begin(), bones.end(), 0);
        out.places.assign(bones.size(), glm::mat4(1.0f));
        return { std::move(out), std::move(bones) };
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
        if (!meshes.skins.empty())
        {
            if (std::ranges::equal(meshes.places, places))
                return;
            meshes.places.assign(places.begin(), places.end());
            if (places.empty())
                return;
            // Plain arithmetic, as below. Most vertices follow one bone whole and need no blend.
            for (std::size_t v = 0; v < meshes.own.size() && v < meshes.skins.size(); ++v)
            {
                const VertexSkin& skin = meshes.skins[v];
                glm::vec4 blend[4]; // Columns of the blended place, when there is a blend
                const glm::vec4* c = &places[skin.bones[0] < places.size() ? skin.bones[0] : 0][0];
                if (skin.weights[0] != 255)
                {
                    float m[16] { };
                    float total = 0;
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        if (skin.weights[k] == 0 || skin.bones[k] >= places.size())
                            continue;
                        const float w = static_cast<float>(skin.weights[k]) / 255.0f;
                        const float* from = &places[skin.bones[k]][0][0];
                        for (std::size_t e = 0; e < 16; ++e)
                            m[e] += w * from[e];
                        total += w;
                    }
                    if (total > 0)
                    {
                        for (std::size_t col = 0; col < 4; ++col)
                            blend[col] = { m[col * 4], m[col * 4 + 1], m[col * 4 + 2], m[col * 4 + 3] };
                        c = blend;
                    }
                }
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
                // Blended turns shorten a normal.
                const float length = c == blend ? std::sqrt(to_n.x * to_n.x + to_n.y * to_n.y + to_n.z * to_n.z) : 0.0f;
                if (length > 0)
                    to_n /= length;
            }
            return;
        }
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
