// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/off_map_trees.hpp"

#include "park/object_mesh.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

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
                tree.rotation = 0;
                tree.base_height = 0;
                tree.height_adjust = 0.0f;
                tree.height_offset = 0;
                tree.force_absolute_height = true;
                const glm::vec3 foot = object_transform(tree, positioning, grid)[3];
                const auto ground = land_height(land, grid, foot.x, foot.z);
                if (!ground || flatness(foot.x, foot.z, *ground) < kOffMapFlatness)
                    continue; // On the map, past the land, under the sea or too steep
                const float y = *ground - kTreeSink;
                tree.base_height = static_cast<std::int32_t>(std::floor(y));
                tree.height_adjust = y - std::floor(y);
                tree.id = ++last_id;
                tree.owner_ref = tree.id;
                out.push_back(std::move(tree));
            }
        }
        return out;
    }

} // namespace rct3::park
