// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/animal_mesh.hpp"

#include "core/types.hpp"

#include <algorithm>

namespace rct3::park
{

    std::optional<usize> animal_level(f32 distance, object::LevelScale scale)
    {
        const f32 last = kAnimalDistances.back() * scale.distance;
        for (usize k = 0; k < kAnimalDistances.size(); ++k)
            if (distance <= (k + 1 < kAnimalDistances.size() ? std::min(kAnimalDistances[k] * scale.detail, last) : last))
                return k;
        return std::nullopt;
    }

    object::ObjectMesh animal_mesh(std::span<const PlacedAnimal> animals, AnimalRuns& runs)
    {
        for (auto& [picture, run] : runs)
        {
            run.vertices.clear();
            run.indices.clear();
        }
        object::ObjectMesh mesh;
        for (const PlacedAnimal& animal : animals)
        {
            if (animal.body == nullptr || animal.body->levels.empty())
                continue;
            // The level asked for, or the coarsest one with something to draw.
            usize level = std::min(animal.level, animal.body->levels.size() - 1);
            while (level > 0 && animal.body->levels[level].empty())
                --level;
            const std::vector<glm::mat4> pose = animal_pose(*animal.body, animal.motion, animal.time.value);
            const glm::mat3 turn(animal.world);
            for (const AnimalPiece& piece : animal.body->levels[level])
            {
                AnimalRun& run = runs[piece.picture];
                run.blend = piece.blend;
                run.one_sided = piece.one_sided;
                const auto base = static_cast<u32>(run.vertices.size());
                for (const AnimalVertex& v : piece.vertices)
                {
                    glm::mat4 skin(0.0f);
                    f32 total = 0;
                    for (usize k = 0; k < 4; ++k)
                    {
                        if (v.weights[k] == 0 || v.bones[k] >= pose.size())
                            continue;
                        const f32 w = static_cast<f32>(v.weights[k]) / 255.0f;
                        skin += w * pose[v.bones[k]];
                        total += w;
                    }
                    if (total <= 0)
                        skin = glm::mat4(1.0f);
                    const glm::vec3 at = animal.world * skin * glm::vec4(v.position, 1.0f);
                    const glm::vec3 normal = turn * glm::mat3(skin) * v.normal;
                    const f32 length = glm::length(normal);
                    run.vertices.push_back({ at, glm::vec4(1.0f), v.uv, length > 0 ? normal / length : normal });
                }
                for (const u16 i : piece.indices)
                    run.indices.push_back(base + i);
            }
            ++mesh.drawn;
        }
        for (const object::PartBlend blend : { object::PartBlend::Opaque, object::PartBlend::Cutout, object::PartBlend::Blended })
        {
            for (const auto& [picture, run] : runs)
            {
                if (run.blend != blend || run.indices.empty())
                    continue;
                const auto base = static_cast<u32>(mesh.vertices.size());
                mesh.batches.push_back({ .texture = picture, .picture = picture, .blend = blend, .first = static_cast<u32>(mesh.indices.size()), .count = static_cast<u32>(run.indices.size()), .one_sided = run.one_sided });
                mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
                for (const u32 i : run.indices)
                    mesh.indices.push_back(base + i);
            }
        }
        return mesh;
    }

} // namespace rct3::park
