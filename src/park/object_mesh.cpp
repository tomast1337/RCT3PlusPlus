// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_mesh.hpp"

#include "park/ground_textures.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <tuple>
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
            std::vector<BillboardStand> stands; ///< Billboard runs only
            std::vector<std::uint32_t> owners;  ///< By vertex
        };

        using Key = std::tuple<PartBlend, std::string, bool, std::int32_t>; // Blend, painted picture, one-sided, chain run

        FlexiColours colours_of(const save::PlacedObject& object, const ColourOverrides& overrides)
        {
            const auto custom = overrides.find(object.id);
            return custom != overrides.end() ? custom->second : flexi_colours(object.colours);
        }

        /// What `texture` paints like, when it is one of the ground's stand-ins.
        std::optional<GroundPaint> ground_paint_of(std::string_view texture)
        {
            if (texture == kGroundSurfacePicture)
                return GroundPaint::Surface;
            if (texture == kGroundCliffPicture)
                return GroundPaint::Cliff;
            return std::nullopt;
        }

        std::string ground_texture(GroundPaint paint, const save::TerrainGrid& grid, glm::vec2 anchor)
        {
            const int col = grid.tile_size_x != 0 ? static_cast<int>(std::floor((anchor.x - grid.origin_x) / grid.tile_size_x)) : -1;
            const int row = grid.tile_size_z != 0 ? static_cast<int>(std::floor((anchor.y - grid.origin_z) / grid.tile_size_z)) : -1;
            if (!grid.contains(col, row))
                return ground_key(paint, 0);
            const save::TerrainCell& cell = grid.at(col, row);
            return ground_key(paint, paint == GroundPaint::Cliff ? cell.cliff : cell.surface);
        }

        /// Where a point of a ground-painted face falls on its picture, as the terrain mesh lays it.
        glm::vec2 ground_uv(const glm::vec3& at, const glm::vec3& normal)
        {
            if (std::abs(normal.y) >= std::max(std::abs(normal.x), std::abs(normal.z)))
                return { at.x / kTextureSpan, at.z / kTextureSpan };
            const bool along_x = std::abs(normal.z) > std::abs(normal.x);
            return { (along_x ? at.x : at.z) / kTextureSpan, -at.y / kTextureSpan };
        }

        void bake(std::map<Key, Run>& runs, ObjectMesh& mesh)
        {
            for (auto& [key, run] : runs)
            {
                const auto& [blend, texture, one_sided, chain] = key;
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.batches.push_back({ texture, run.picture, run.colours, blend, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(run.indices.size()), one_sided, chain });
                mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
                mesh.owners.insert(mesh.owners.end(), run.owners.begin(), run.owners.end());
                for (const std::uint32_t i : run.indices)
                    mesh.indices.push_back(base + i);
            }
        }

    } // namespace

    std::string model_key(const save::PlacedObject& object)
    {
        return std::format("{}|{}", object.overlay, object.symbol);
    }

    ModelRef model_ref(const save::PlacedObject& object)
    {
        return { object.overlay, object.symbol };
    }

    namespace
    {
        /// How the key `ref` stands for compares with `key`, part by part as if joined.
        std::strong_ordering compare(ModelRef ref, std::string_view key)
        {
            for (const std::string_view part : { ref.overlay, std::string_view("|"), ref.symbol })
            {
                if (const int c = part.compare(key.substr(0, part.size())); c != 0)
                    return c <=> 0;
                key.remove_prefix(part.size());
            }
            return key.empty() ? std::strong_ordering::equal : std::strong_ordering::less;
        }
    } // namespace

    bool ModelKeyOrder::operator()(ModelRef a, std::string_view b) const noexcept
    {
        return compare(a, b) < 0;
    }

    bool ModelKeyOrder::operator()(std::string_view a, ModelRef b) const noexcept
    {
        return compare(b, a) > 0;
    }

    glm::mat4 placed_transform(const save::PlacedObject& object, const ObjectModel* model, const save::TerrainGrid& grid, const Fitted& fitted)
    {
        const auto found = fitted.find(object.id);
        if (found != fitted.end())
            return found->second;
        if (!model)
            return object_transform(object, Positioning::Tile, grid);
        return object_transform(object, static_cast<Positioning>(model->positioning), grid, model->squares, model->ground_height);
    }

    std::vector<std::size_t> update_places(ObjectPlaces& places, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted)
    {
        const std::size_t had = places.models.size();
        const bool more_models = models.size() != places.model_count;
        places.keys.resize(objects.size());
        places.models.resize(objects.size(), nullptr);
        places.transforms.resize(objects.size());
        places.model_count = models.size();
        std::vector<std::size_t> changed;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const save::PlacedObject& o = objects[i];
            std::string& key = places.keys[i];
            // The key as model_key would build it, compared without building it.
            const bool renamed = i >= had || key.size() != o.overlay.size() + 1 + o.symbol.size() || !key.starts_with(o.overlay) || key[o.overlay.size()] != '|' || !key.ends_with(o.symbol);
            const ObjectModel* model = i < had ? places.models[i] : nullptr;
            if (renamed || (!model && more_models))
            {
                if (renamed)
                    key = model_key(o);
                const auto found = models.find(model_ref(o));
                model = found != models.end() ? &found->second : nullptr;
            }
            const glm::mat4 transform = placed_transform(o, model, grid, fitted);
            if (i < had && model == places.models[i] && transform == places.transforms[i])
                continue;
            places.models[i] = model;
            places.transforms[i] = transform;
            changed.push_back(i);
        }
        return changed;
    }

    std::vector<glm::vec3> object_anchors(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted)
    {
        std::vector<glm::vec3> anchors;
        anchors.reserve(objects.size());
        for (const save::PlacedObject& object : objects)
        {
            const auto model = models.find(model_ref(object));
            anchors.emplace_back(placed_transform(object, model != models.end() ? &model->second : nullptr, grid, fitted)[3]);
        }
        return anchors;
    }

    std::vector<Lamp> object_lamps(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, std::span<const glm::mat4> places, std::span<const ObjectModel* const> found)
    {
        std::vector<Lamp> lamps;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const save::PlacedObject& object = objects[i];
            if (object.hidden)
                continue;
            const ObjectModel* model = i < found.size() ? found[i] : nullptr;
            if (i >= found.size())
            {
                const auto at = models.find(model_ref(object));
                model = at != models.end() ? &at->second : nullptr;
            }
            if (!model || model->lights.empty())
                continue;
            const glm::mat4 placed = i < places.size() ? places[i] : placed_transform(object, model, grid, fitted);
            for (const ModelLight& light : model->lights)
            {
                glm::vec3 colour = light.colour;
                if (light.colour_slot)
                {
                    const Rgb c = flexi_colour(static_cast<std::size_t>(std::max(object.colours[static_cast<std::size_t>(*light.colour_slot)], 0)));
                    colour = glm::vec3(c.r, c.g, c.b) / 255.0f;
                }
                lamps.push_back({ .position = glm::vec3(placed * glm::vec4(light.position, 1.0f)), .radius = light.radius, .colour = colour });
            }
        }
        return lamps;
    }

    std::vector<PlacedEffect> object_effects(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, std::span<const glm::mat4> places, std::span<const ObjectModel* const> found)
    {
        std::vector<PlacedEffect> effects;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (objects[i].hidden)
                continue;
            const ObjectModel* model = i < found.size() ? found[i] : nullptr;
            if (i >= found.size())
            {
                const auto at = models.find(model_ref(objects[i]));
                model = at != models.end() ? &at->second : nullptr;
            }
            if (!model || model->effects.empty())
                continue;
            const glm::mat4 placed = i < places.size() ? places[i] : placed_transform(objects[i], model, grid, fitted);
            for (const ModelEffect& e : model->effects)
            {
                const glm::vec3 up = glm::normalize(glm::mat3(placed) * e.up);
                const glm::vec3 side = glm::normalize(glm::cross(up, std::abs(up.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                effects.push_back({ .effect = e.effect, .position = glm::vec3(placed * glm::vec4(e.position, 1.0f)), .axes = glm::mat3(side, up, glm::cross(side, up)), .object = i });
            }
        }
        return effects;
    }

    LevelPlan plan_levels(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, const glm::vec3& eye, float band, const LevelScales& scales)
    {
        LevelPlan plan { .steady = ObjectLevels(objects.size(), kNotDrawn), .finer = ObjectLevels(objects.size(), kNotDrawn), .coarser = ObjectLevels(objects.size(), kNotDrawn), .bands = std::vector<glm::vec2>(objects.size()) };
        for (std::size_t i = 0; i < objects.size() && i < anchors.size(); ++i)
        {
            if (objects[i].hidden)
                continue;
            const auto model = models.find(model_ref(objects[i]));
            if (model == models.end() || model->second.levels.empty())
                continue;
            const auto& levels = model->second.levels;
            const float distance = glm::distance(eye, anchors[i]);
            const LevelScale scale = scales[static_cast<std::size_t>(detail_kind(model->second.category))];
            const auto level = level_at(model->second, distance, scale);
            // The edges the object may be crossing: past the end of its own level, or just past the one before.
            const std::size_t last = levels.size() - 1;
            const std::size_t at = level.value_or(last + 1);
            for (const std::size_t edge : { at, at - 1 })
            {
                if (band <= 0)
                    break;
                if (edge > last)
                    continue;
                const float far = level_reach(model->second, edge, scale);
                const float near = far * (1 - band);
                if (!std::isfinite(far) || far <= 0 || distance < near - far * kCrossMargin || distance > far + far * kCrossMargin)
                    continue;
                plan.finer[i] = static_cast<std::int8_t>(edge);
                plan.coarser[i] = edge < last ? static_cast<std::int8_t>(edge + 1) : kNotDrawn;
                plan.bands[i] = { near, far };
                break;
            }
            if (plan.finer[i] == kNotDrawn && level)
                plan.steady[i] = static_cast<std::int8_t>(*level);
        }
        return plan;
    }

    ObjectLevels object_levels(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, const glm::vec3& eye, const LevelScales& scales)
    {
        ObjectLevels levels(objects.size(), kNotDrawn);
        for (std::size_t i = 0; i < objects.size() && i < anchors.size(); ++i)
        {
            if (objects[i].hidden)
                continue;
            const auto model = models.find(model_ref(objects[i]));
            if (model == models.end())
                continue;
            if (const auto level = level_at(model->second, glm::distance(eye, anchors[i]), scales[static_cast<std::size_t>(detail_kind(model->second.category))]))
                levels[i] = static_cast<std::int8_t>(*level);
        }
        return levels;
    }

    ObjectMesh object_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, std::span<const std::int8_t> levels, const ColourOverrides& overrides, const Leans& leans, const Fitted& fitted, const Broken& broken, const Chains& chains)
    {
        const bool finest = levels.size() != objects.size();
        ObjectMesh mesh;
        std::map<Key, Run> runs;
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            const save::PlacedObject& object = objects[index];
            if (object.hidden)
                continue;
            const auto model = models.find(model_ref(object));
            if (model == models.end())
            {
                ++mesh.missing;
                continue;
            }
            const std::int8_t level = finest ? 0 : levels[index];
            if (level < 0 || static_cast<std::size_t>(level) >= model->second.levels.size())
                continue;
            const bool worn = !model->second.broken_levels.empty() && broken.contains(object.id);
            const ModelLevel& shown = (worn ? model->second.broken_levels : model->second.levels)[static_cast<std::size_t>(level)];
            if (shown.parts.empty())
                continue;
            ++mesh.placed;
            const glm::mat4 transform = placed_transform(object, &model->second, grid, fitted);
            // A fitted piece may be stretched: its normals take the inverse transpose to stay square to its faces.
            const bool stretched = fitted.contains(object.id);
            const glm::mat3 turn = stretched ? glm::transpose(glm::inverse(glm::mat3 { transform })) : glm::mat3 { transform };
            const FlexiColours colours = colours_of(object, overrides);
            const auto lean = leans.find(object.id);
            const glm::vec2 rise = lean != leans.end() ? lean->second : glm::vec2(0.0f);
            const glm::vec2 anchor { transform[3].x, transform[3].z };
            // Plants, rocks and the like on the ground bend to it: each point keeps its height above the ground.
            const bool fit = model->second.fits_ground && !stretched && on_ground(object);
            const auto chain = chains.find(object.id);
            for (const ModelPart& part : shown.parts)
            {
                const auto paint = ground_paint_of(part.texture);
                Run& run = runs[{ part.blend, paint ? ground_texture(*paint, grid, anchor) : painted_name(part.texture, part.slots, colours), part.one_sided, part.chain && chain != chains.end() ? chain->second : 0 }];
                run.picture = part.texture;
                run.colours = used_colours(part.slots, colours);
                const auto base = static_cast<std::uint32_t>(run.vertices.size());
                for (const ModelVertex& v : part.vertices)
                {
                    glm::vec3 at { transform * glm::vec4(v.position, 1.0f) };
                    at.y += glm::dot(glm::vec2(at.x, at.z) - anchor, rise);
                    if (fit)
                    {
                        if (const auto ground = grid.height_at(at.x, at.z))
                            at.y += *ground - transform[3].y;
                    }
                    const glm::vec3 normal = stretched ? glm::normalize(turn * v.normal) : turn * v.normal;
                    run.vertices.push_back({ at, glm::vec4(1.0f), paint ? ground_uv(at, normal) : v.uv, normal });
                }
                run.owners.resize(run.vertices.size(), static_cast<std::uint32_t>(index));
                for (const std::uint32_t i : part.indices)
                    run.indices.push_back(base + i);
            }
        }

        bake(runs, mesh);
        return mesh;
    }

    Billboards gather_billboards(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, std::span<const std::int8_t> levels, const ColourOverrides& overrides)
    {
        Billboards boards;
        std::map<Key, Run> runs;
        for (std::size_t index = 0; index < objects.size() && index < levels.size() && index < anchors.size(); ++index)
        {
            if (levels[index] < 0)
                continue;
            const auto model = models.find(model_ref(objects[index]));
            if (model == models.end() || static_cast<std::size_t>(levels[index]) >= model->second.levels.size())
                continue;
            const auto& board = model->second.levels[static_cast<std::size_t>(levels[index])].billboard;
            if (!board)
                continue;
            ++boards.mesh.placed;
            const FlexiColours colours = colours_of(objects[index], overrides);
            Run& run = runs[{ PartBlend::Cutout, painted_name(board->texture, board->slots, colours), false, 0 }];
            run.picture = board->texture;
            run.colours = used_colours(board->slots, colours);
            const auto first = static_cast<std::uint32_t>(run.vertices.size());
            // Without a normal, so it is lit as a surface facing up and turning round a tree does not change its
            // shade.
            const glm::vec4 shade { 1.0f };
            run.vertices.push_back({ glm::vec3(0.0f), shade, board->uv_min });
            run.vertices.push_back({ glm::vec3(0.0f), shade, { board->uv_max.x, board->uv_min.y } });
            run.vertices.push_back({ glm::vec3(0.0f), shade, board->uv_max });
            run.vertices.push_back({ glm::vec3(0.0f), shade, { board->uv_min.x, board->uv_max.y } });
            for (const std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
                run.indices.push_back(first + i);
            run.owners.resize(run.vertices.size(), static_cast<std::uint32_t>(index));
            run.stands.push_back({ anchors[index], board->width / 2, board->height });
        }
        for (const auto& [_, run] : runs)
            boards.stands.insert(boards.stands.end(), run.stands.begin(), run.stands.end());
        bake(runs, boards.mesh);
        return boards;
    }

    void face_billboards(Billboards& boards, const glm::vec3& eye)
    {
        for (std::size_t k = 0; k < boards.stands.size() && k * 4 + 3 < boards.mesh.vertices.size(); ++k)
        {
            const BillboardStand& stand = boards.stands[k];
            glm::vec2 facing { eye.x - stand.base.x, eye.z - stand.base.z };
            const float length = glm::length(facing);
            facing = length < 1e-4f ? glm::vec2 { 0, 1 } : facing / length;
            const glm::vec3 right = glm::vec3(facing.y, 0, -facing.x) * stand.half_width;
            const glm::vec3 up { 0, stand.height, 0 };
            GroundVertex* corners = &boards.mesh.vertices[k * 4];
            corners[0].position = stand.base - right + up;
            corners[1].position = stand.base + right + up;
            corners[2].position = stand.base + right;
            corners[3].position = stand.base - right;
        }
    }

    ObjectMesh billboard_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, std::span<const std::int8_t> levels, const glm::vec3& eye, const ColourOverrides& overrides)
    {
        Billboards boards = gather_billboards(objects, models, anchors, levels, overrides);
        face_billboards(boards, eye);
        return std::move(boards.mesh);
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
