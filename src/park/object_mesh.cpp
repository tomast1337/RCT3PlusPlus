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

        FlexiColours colours_of(const save::PlacedObject& object, const ColourOverrides& overrides)
        {
            const auto custom = overrides.find(object.id);
            return custom != overrides.end() ? custom->second : flexi_colours(object.colours);
        }

        void bake(std::map<Key, Run>& runs, ObjectMesh& mesh)
        {
            for (auto& [key, run] : runs)
            {
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.batches.push_back({ key.second, run.picture, run.colours, key.first, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(run.indices.size()) });
                mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
                for (const std::uint32_t i : run.indices)
                    mesh.indices.push_back(base + i);
            }
        }

    } // namespace

    std::string model_key(const save::PlacedObject& object)
    {
        return object.overlay + "|" + object.symbol;
    }

    std::vector<glm::vec3> object_anchors(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        std::vector<glm::vec3> anchors;
        anchors.reserve(objects.size());
        for (const save::PlacedObject& object : objects)
        {
            const auto model = models.find(model_key(object));
            const auto positioning = static_cast<Positioning>(model != models.end() ? model->second.positioning : 0);
            anchors.emplace_back(object_transform(object, positioning, grid)[3]);
        }
        return anchors;
    }

    ObjectLevels object_levels(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, const glm::vec3& eye, float scale)
    {
        ObjectLevels levels(objects.size(), kNotDrawn);
        for (std::size_t i = 0; i < objects.size() && i < anchors.size(); ++i)
        {
            if (objects[i].hidden)
                continue;
            const auto model = models.find(model_key(objects[i]));
            if (model == models.end())
                continue;
            if (const auto level = level_at(model->second, glm::distance(eye, anchors[i]), scale))
                levels[i] = static_cast<std::int8_t>(*level);
        }
        return levels;
    }

    ObjectMesh object_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, std::span<const std::int8_t> levels, const ColourOverrides& overrides, const Leans& leans)
    {
        const bool finest = levels.size() != objects.size();
        ObjectMesh mesh;
        std::map<Key, Run> runs;
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            const save::PlacedObject& object = objects[index];
            if (object.hidden)
                continue;
            const auto model = models.find(model_key(object));
            if (model == models.end())
            {
                ++mesh.missing;
                continue;
            }
            const std::int8_t level = finest ? 0 : levels[index];
            if (level < 0 || static_cast<std::size_t>(level) >= model->second.levels.size())
                continue;
            const ModelLevel& shown = model->second.levels[static_cast<std::size_t>(level)];
            if (shown.parts.empty())
                continue;
            ++mesh.placed;
            const glm::mat4 transform = object_transform(object, static_cast<Positioning>(model->second.positioning), grid);
            const glm::mat3 turn { transform };
            const FlexiColours colours = colours_of(object, overrides);
            const auto lean = leans.find(object.id);
            const glm::vec2 rise = lean != leans.end() ? lean->second : glm::vec2(0.0f);
            const glm::vec2 anchor { transform[3].x, transform[3].z };
            for (const ModelPart& part : shown.parts)
            {
                Run& run = runs[{ part.blend, painted_name(part.texture, part.slots, colours) }];
                run.picture = part.texture;
                run.colours = used_colours(part.slots, colours);
                const auto base = static_cast<std::uint32_t>(run.vertices.size());
                for (const ModelVertex& v : part.vertices)
                {
                    const float s = sunlight(turn * v.normal);
                    glm::vec3 at { transform * glm::vec4(v.position, 1.0f) };
                    at.y += glm::dot(glm::vec2(at.x, at.z) - anchor, rise);
                    run.vertices.push_back({ at, { s, s, s, 1 }, v.uv });
                }
                for (const std::uint32_t i : part.indices)
                    run.indices.push_back(base + i);
            }
        }

        bake(runs, mesh);
        return mesh;
    }

    ObjectMesh billboard_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, std::span<const std::int8_t> levels, const glm::vec3& eye, const ColourOverrides& overrides)
    {
        // Lit as a surface facing up, so turning round a tree does not change its shade.
        const float s = sunlight({ 0, 1, 0 });
        ObjectMesh mesh;
        std::map<Key, Run> runs;
        for (std::size_t index = 0; index < objects.size() && index < levels.size() && index < anchors.size(); ++index)
        {
            if (levels[index] < 0)
                continue;
            const auto model = models.find(model_key(objects[index]));
            if (model == models.end() || static_cast<std::size_t>(levels[index]) >= model->second.levels.size())
                continue;
            const auto& board = model->second.levels[static_cast<std::size_t>(levels[index])].billboard;
            if (!board)
                continue;
            ++mesh.placed;
            const glm::vec3 base = anchors[index];
            glm::vec2 facing { eye.x - base.x, eye.z - base.z };
            if (glm::length(facing) < 1e-4f)
                facing = { 0, 1 };
            facing = glm::normalize(facing);
            const glm::vec3 right = glm::vec3(facing.y, 0, -facing.x) * (board->width / 2);
            const glm::vec3 up { 0, board->height, 0 };

            const FlexiColours colours = colours_of(objects[index], overrides);
            Run& run = runs[{ PartBlend::Cutout, painted_name(board->texture, board->slots, colours) }];
            run.picture = board->texture;
            run.colours = used_colours(board->slots, colours);
            const auto first = static_cast<std::uint32_t>(run.vertices.size());
            const glm::vec4 shade { s, s, s, 1 };
            run.vertices.push_back({ base - right + up, shade, board->uv_min });
            run.vertices.push_back({ base + right + up, shade, { board->uv_max.x, board->uv_min.y } });
            run.vertices.push_back({ base + right, shade, board->uv_max });
            run.vertices.push_back({ base - right, shade, { board->uv_min.x, board->uv_max.y } });
            for (const std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
                run.indices.push_back(first + i);
        }
        bake(runs, mesh);
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
