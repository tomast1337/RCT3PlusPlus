// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_mesh.hpp"

#include "park/lighting.hpp"
#include "park/object_placement.hpp"

#include <utility>

namespace rct3::park
{

    namespace
    {

        struct Run
        {
            std::string picture;
            FlexiColours colours { };
            std::vector<GroundVertex> vertices;
            std::vector<std::uint32_t> indices;
        };

        using Key = std::pair<PartBlend, std::string>;

    } // namespace

    std::string model_key(const save::PlacedObject& object)
    {
        return object.overlay + "|" + object.symbol;
    }

    ObjectMesh object_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        ObjectMesh mesh;
        std::map<Key, Run> runs;
        for (const save::PlacedObject& object : objects)
        {
            if (object.hidden)
                continue;
            const auto model = models.find(model_key(object));
            if (model == models.end())
            {
                ++mesh.missing;
                continue;
            }
            ++mesh.placed;
            const glm::mat4 transform = object_transform(object, static_cast<Positioning>(model->second.positioning), grid);
            const glm::mat3 turn { transform };
            const FlexiColours colours = flexi_colours(object.colours);
            for (const ModelPart& part : model->second.parts)
            {
                Run& run = runs[{ part.blend, painted_name(part.texture, part.slots, colours) }];
                run.picture = part.texture;
                run.colours = used_colours(part.slots, colours);
                const auto base = static_cast<std::uint32_t>(run.vertices.size());
                for (const ModelVertex& v : part.vertices)
                {
                    const float s = sunlight(turn * v.normal);
                    run.vertices.push_back({ glm::vec3(transform * glm::vec4(v.position, 1.0f)), { s, s, s, 1 }, v.uv });
                }
                for (const std::uint32_t i : part.indices)
                    run.indices.push_back(base + i);
            }
        }

        for (auto& [key, run] : runs)
        {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.batches.push_back({ key.second, run.picture, run.colours, key.first, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(run.indices.size()) });
            mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
            for (const std::uint32_t i : run.indices)
                mesh.indices.push_back(base + i);
        }
        return mesh;
    }

    std::map<std::string, ovl::RgbaImage, std::less<>> paint_batches(std::span<const ObjectBatch> batches, const ModelPictures& pictures)
    {
        std::map<std::string, ovl::RgbaImage, std::less<>> out;
        for (const ObjectBatch& batch : batches)
        {
            if (out.contains(batch.texture))
                continue;
            const auto picture = pictures.find(batch.picture);
            if (picture != pictures.end())
                out.emplace(batch.texture, paint(picture->second, batch.colours));
        }
        return out;
    }

} // namespace rct3::park
