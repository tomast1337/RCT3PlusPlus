// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/animal_mesh.hpp"

namespace rct3::park
{

    ObjectMesh animal_mesh(std::span<const PlacedAnimal> animals, AnimalRuns& runs)
    {
        for (auto& [picture, run] : runs)
        {
            run.vertices.clear();
            run.indices.clear();
        }
        ObjectMesh mesh;
        for (const PlacedAnimal& animal : animals)
        {
            if (animal.body == nullptr || animal.body->levels.empty())
                continue;
            // The level asked for, or the coarsest one with something to draw.
            std::size_t level = std::min(animal.level, animal.body->levels.size() - 1);
            while (level > 0 && animal.body->levels[level].empty())
                --level;
            const std::vector<glm::mat4> pose = animal_pose(*animal.body, animal.motion, animal.time);
            const glm::mat3 turn(animal.world);
            for (const AnimalPiece& piece : animal.body->levels[level])
            {
                AnimalRun& run = runs[piece.picture];
                run.blend = piece.blend;
                run.one_sided = piece.one_sided;
                const auto base = static_cast<std::uint32_t>(run.vertices.size());
                for (const AnimalVertex& v : piece.vertices)
                {
                    glm::mat4 skin(0.0f);
                    float total = 0;
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        if (v.weights[k] == 0 || v.bones[k] >= pose.size())
                            continue;
                        const float w = static_cast<float>(v.weights[k]) / 255.0f;
                        skin += w * pose[v.bones[k]];
                        total += w;
                    }
                    if (total <= 0)
                        skin = glm::mat4(1.0f);
                    const glm::vec3 at = animal.world * skin * glm::vec4(v.position, 1.0f);
                    const glm::vec3 normal = turn * glm::mat3(skin) * v.normal;
                    const float length = glm::length(normal);
                    run.vertices.push_back({ at, glm::vec4(1.0f), v.uv, length > 0 ? normal / length : normal });
                }
                for (const std::uint16_t i : piece.indices)
                    run.indices.push_back(base + i);
            }
            ++mesh.placed;
        }
        for (const PartBlend blend : { PartBlend::Opaque, PartBlend::Cutout, PartBlend::Blended })
        {
            for (const auto& [picture, run] : runs)
            {
                if (run.blend != blend || run.indices.empty())
                    continue;
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.batches.push_back({ .texture = picture, .picture = picture, .blend = blend, .first = static_cast<std::uint32_t>(mesh.indices.size()), .count = static_cast<std::uint32_t>(run.indices.size()), .one_sided = run.one_sided });
                mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
                for (const std::uint32_t i : run.indices)
                    mesh.indices.push_back(base + i);
            }
        }
        return mesh;
    }

} // namespace rct3::park
