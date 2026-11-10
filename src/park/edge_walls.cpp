// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/edge_walls.hpp"

#include "park/item_category.hpp"
#include "park/object/placement.hpp"
#include "park/path/furniture.hpp"

#include <algorithm>
#include <array>

namespace rct3::park
{

    namespace
    {
        /// How high a fence that says nothing of its own height stands, in world units.
        constexpr float kFenceHeight = 2;
        /// How far below its base a wall still meets a path's edge, for a path on ground that falls along it.
        constexpr float kBelow = 0.5f;

        std::uint64_t key(save::Tile tile, Side side)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.row)) << 16 | static_cast<std::uint32_t>(tile.col)) << 2 | static_cast<std::uint32_t>(side.value);
        }
    } // namespace

    std::optional<EdgeWall> edge_wall(const save::PlacedObject& o, const object::ObjectModels& models, const save::TerrainGrid& grid)
    {
        if (o.hidden)
            return std::nullopt;
        const auto model = models.find(object::model_ref(o));
        if (model == models.end())
            return std::nullopt;
        const object::ObjectModel& m = model->second;
        if (static_cast<object::Positioning>(m.positioning) != object::Positioning::Edge || (m.category != std::to_underlying(ItemCategory::Fence) && m.category != std::to_underlying(ItemCategory::Wall)))
            return std::nullopt;
        const float base = object::object_transform(o, object::Positioning::Edge, grid, m.squares, m.ground_height)[3].y;
        std::int32_t top = 0;
        for (const auto& levels : m.solid)
            top = std::max(top, levels ? (*levels)[1] + 1 : 0);
        return EdgeWall { .tile = { o.col, o.row }, .side = object::faced_side(ModelTurn::from(o.rotation)), .low = base - kBelow, .high = base + (top > 0 ? static_cast<float>(top) : kFenceHeight) };
    }

    EdgeWalls edge_walls(std::span<const save::PlacedObject> objects, const object::ObjectModels& models, const save::TerrainGrid& grid)
    {
        EdgeWalls walls;
        for (const save::PlacedObject& o : objects)
        {
            const auto wall = edge_wall(o, models, grid);
            if (!wall)
                continue;
            walls.emplace(key(wall->tile, wall->side), *wall);
            const Side side = wall->side.opposite();
            const save::Tile across = wall->side.beyond(wall->tile);
            walls.emplace(key(across, side), EdgeWall { .tile = across, .side = side, .low = wall->low, .high = wall->high });
        }
        return walls;
    }

    bool walled(const EdgeWalls& walls, save::Tile tile, Side side, float height)
    {
        const auto [first, last] = walls.equal_range(key(tile, side));
        return std::any_of(first, last, [&](const auto& w) { return height >= w.second.low && height < w.second.high; });
    }

} // namespace rct3::park
