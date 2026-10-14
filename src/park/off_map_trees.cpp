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

        /// A point `d` beyond the map's edge, a share `t` of the way round the rectangle grown by `d`.
        glm::vec2 round_the_edge(const save::TerrainGrid& g, float d, float t)
        {
            const float x0 = g.world_x(0) - d;
            const float z0 = g.world_z(0) - d;
            const float w = g.world_x(g.width) + d - x0;
            const float h = g.world_z(g.height) + d - z0;
            float along = t * 2 * (w + h);
            if (along < w)
                return { x0 + along, z0 };
            along -= w;
            if (along < h)
                return { x0 + w, z0 + along };
            along -= h;
            if (along < w)
                return { x0 + w - along, z0 + h };
            along -= w;
            return { x0, z0 + h - along };
        }

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

    } // namespace

    bool is_plant(const save::PlacedObject& object, const ObjectModels& models)
    {
        const auto model = models.find(model_key(object));
        if (model == models.end())
            return false;
        return model->second.sway > 0;
    }

    std::vector<save::PlacedObject> off_map_trees(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const LandRings& land, random::RandomState& scenery)
    {
        std::vector<save::PlacedObject> out;
        std::vector<const save::PlacedObject*> kinds;
        std::uint64_t last_id = 0;
        for (const save::PlacedObject& o : objects)
        {
            last_id = std::max(last_id, o.id);
            if (!o.hidden && o.free_standing() && is_plant(o, models))
                kinds.push_back(&o);
        }
        if (kinds.empty() || grid.empty())
            return out;

        const auto groves = std::min(kMaxGroves, static_cast<std::size_t>(std::ceil(static_cast<float>(kinds.size()) * kGrovesPerParkTree)));
        for (std::size_t g = 0; g < groves; ++g)
        {
            // Picking a park tree, not a kind, makes the commoner kinds commoner outside too.
            const save::PlacedObject& kind = *kinds[random::below(scenery, kinds.size())];
            const auto model = models.find(model_key(kind));
            const auto positioning = static_cast<Positioning>(model->second.positioning);
            // Squaring a uniform draw puts more groves near the edge.
            const auto u = static_cast<float>(random::unit(scenery));
            const glm::vec2 middle = round_the_edge(grid, kOffMapTreeReach * u * u, static_cast<float>(random::unit(scenery)));
            const auto count = random::between(scenery, kMinGrove, kMaxGrove);
            for (std::int64_t i = 0; i < count; ++i)
            {
                const auto angle = static_cast<float>(random::between(scenery, 0.0, 2 * std::numbers::pi));
                const auto reach = static_cast<float>(random::between(scenery, 0.0, kGroveSpread));
                save::PlacedObject tree = kind;
                place_at(tree, grid, middle + reach * glm::vec2(std::cos(angle), std::sin(angle)));
                tree.rotation = static_cast<std::int32_t>(random::below(scenery, 4));
                tree.base_height = 0;
                tree.height_adjust = 0.0f;
                tree.height_offset = 0;
                tree.force_absolute_height = true;
                const glm::vec3 foot = object_transform(tree, positioning, grid)[3];
                const auto ground = land_height(land, grid, foot.x, foot.z);
                if (!ground)
                    continue; // On the map, or past the land
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
