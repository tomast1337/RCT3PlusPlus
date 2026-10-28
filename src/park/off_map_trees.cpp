// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/off_map_trees.hpp"

#include "park/object_mesh.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <unordered_map>

namespace rct3::park
{

    namespace
    {

        /// The tile and quarter of world point `p`, as a placed item stores where it stands.
        void place_at(save::PlacedObject& tree, const save::TerrainGrid& g, glm::vec2 p)
        {
            const float col = (p.x - g.origin_x) / g.tile_size_x;
            const float row = (p.y - g.origin_z) / g.tile_size_z;
            tree.col = static_cast<std::int32_t>(std::floor(col));
            tree.row = static_cast<std::int32_t>(std::floor(row));
            const int right = col - std::floor(col) >= 0.5f ? 1 : 0;
            const int down = row - std::floor(row) >= 0.5f ? 1 : 0;
            tree.quadrant = right + 2 * down;
        }

        /// A tree or plant standing in the park, not one beyond its edge.
        bool park_tree(const save::PlacedObject& o, const ObjectModels& models, const save::TerrainGrid& grid)
        {
            return !o.hidden && o.free_standing() && grid.contains(o.col, o.row) && is_plant(o, models);
        }

        /// Stands `tree`, already placed round the edge, on `land` unturned, as a free-standing item numbered
        /// after `last_id`: where its foot is, or nothing where there is no land under it (on the map, past the
        /// land, or under the sea).
        std::optional<glm::vec3> stand(save::PlacedObject& tree, Positioning positioning, const save::TerrainGrid& grid, const LandRings& land, std::uint64_t last_id)
        {
            tree.rotation = 0;
            tree.base_height = 0;
            tree.height_adjust = 0.0f;
            tree.height_offset = 0;
            tree.force_absolute_height = true;
            const glm::vec3 foot = object_transform(tree, positioning, grid)[3];
            const auto ground = land_height(land, grid, foot.x, foot.z);
            if (!ground)
                return std::nullopt;
            const float y = *ground - kTreeSink;
            tree.base_height = static_cast<std::int32_t>(std::floor(y));
            tree.height_adjust = y - std::floor(y);
            tree.id = last_id + 1;
            tree.owner_ref = tree.id;
            return glm::vec3(foot.x, *ground, foot.z);
        }

    } // namespace

    bool is_plant(const save::PlacedObject& object, const ObjectModels& models)
    {
        const auto model = models.find(model_ref(object));
        if (model == models.end())
            return false;
        return model->second.sway > 0;
    }

    std::vector<save::PlacedObject> off_map_trees(std::span<const save::PlacedObject> objects, std::size_t from, const ObjectModels& models, const save::TerrainGrid& grid, const LandRings& land, random::RandomState& scenery)
    {
        std::vector<save::PlacedObject> out;
        if (grid.empty())
            return out;
        std::uint64_t last_id = 0;
        for (const save::PlacedObject& o : objects)
            last_id = std::max(last_id, o.id);
        const float reach = kOffMapReachShare * (grid.world_x(grid.width) - grid.world_x(0) + grid.world_z(grid.height) - grid.world_z(0));
        // The land's upward share at (x, z), from its heights a step either way; a step onto the map counts as level.
        const auto flatness = [&](float x, float z, float y) {
            constexpr float kStep = 1;
            const auto at = [&](float dx, float dz) { return land_height(land, grid, x + dx, z + dz).value_or(y); };
            const float across = (at(kStep, 0) - at(-kStep, 0)) / (2 * kStep);
            const float down = (at(0, kStep) - at(0, -kStep)) / (2 * kStep);
            return 1 / std::sqrt(1 + across * across + down * down);
        };
        for (const save::PlacedObject& kind : objects.subspan(std::min(from, objects.size())))
        {
            if (!park_tree(kind, models, grid))
                continue;
            const auto positioning = static_cast<Positioning>(models.find(model_ref(kind))->second.positioning);
            const glm::vec3 source = object_transform(kind, positioning, grid)[3];
            for (int i = 0; i < kOffMapTries; ++i)
            {
                // Squaring a uniform draw keeps more of them near their park tree.
                const auto u = static_cast<float>(random::unit(scenery));
                const auto angle = static_cast<float>(random::between(scenery, 0.0, 2 * std::numbers::pi));
                save::PlacedObject tree = kind;
                place_at(tree, grid, glm::vec2(source.x, source.z) + reach * u * u * glm::vec2(std::cos(angle), std::sin(angle)));
                const auto foot = stand(tree, positioning, grid, land, last_id);
                if (foot && flatness(foot->x, foot->z, foot->y) >= kOffMapFlatness)
                {
                    ++last_id;
                    out.push_back(std::move(tree));
                }
            }
        }
        return out;
    }

    std::vector<save::PlacedObject> listed_off_map_trees(std::span<const save::SkirtTree> listed, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const LandRings& land)
    {
        std::vector<save::PlacedObject> out;
        std::unordered_map<std::uint64_t, const save::PlacedObject*> by_id;
        std::uint64_t last_id = 0;
        for (const save::PlacedObject& o : objects)
        {
            last_id = std::max(last_id, o.id);
            by_id.emplace(o.id, &o);
        }
        out.reserve(listed.size());
        for (const save::SkirtTree& t : listed)
        {
            const auto source = by_id.find(t.scenery_ref);
            if (source == by_id.end() || !park_tree(*source->second, models, grid))
                continue;
            save::PlacedObject tree = *source->second;
            place_at(tree, grid, { t.position.x, t.position.z });
            if (stand(tree, static_cast<Positioning>(models.find(model_ref(tree))->second.positioning), grid, land, last_id))
            {
                ++last_id;
                out.push_back(std::move(tree));
            }
        }
        return out;
    }

} // namespace rct3::park
