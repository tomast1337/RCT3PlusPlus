// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_furniture.hpp"

#include "park/path_decks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rct3::park
{

    namespace
    {
        constexpr float kSameHeight = 0.5f;    ///< World units between two edges that still meet
        constexpr std::int32_t kSameLevel = 2; ///< Units between two items' bases that still clash
    } // namespace

    std::optional<FurnitureSpot> furniture_spot(std::span<const save::PathTile> paths, const save::TerrainGrid& grid, glm::vec3 hit, Positioning positioning, ModelTurn turn, PathRules rules)
    {
        if (grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
            return std::nullopt;
        const float fx = (hit.x - grid.origin_x) / grid.tile_size_x;
        const float fz = (hit.z - grid.origin_z) / grid.tile_size_z;
        const save::Tile tile { static_cast<int>(std::floor(fx)), static_cast<int>(std::floor(fz)) };
        if (!grid.contains(tile))
            return std::nullopt;
        // Of the paths on the tile, the one whose middle is nearest the pointer's height.
        const save::PathTile* path = nullptr;
        std::array<std::optional<float>, 4> edges;
        float middle = 0;
        float nearest = std::numeric_limits<float>::max();
        for (const save::PathTile& p : paths)
        {
            if (p.col != tile.col || p.row != tile.row)
                continue;
            std::array<std::optional<float>, 4> e;
            float sum = 0;
            int count = 0;
            for (const Side side : kSides)
            {
                auto& edge = e[static_cast<std::size_t>(side.value)];
                edge = edge_height(p, grid, side);
                if (edge)
                {
                    sum += *edge;
                    ++count;
                }
            }
            if (count == 0 || std::abs(sum / static_cast<float>(count) - hit.y) >= nearest)
                continue;
            path = &p;
            edges = e;
            middle = sum / static_cast<float>(count);
            nearest = std::abs(middle - hit.y);
        }
        if (path == nullptr)
            return std::nullopt;
        FurnitureSpot spot { .tile = tile, .turn = ModelTurn::from(turn.value), .height = static_cast<std::int32_t>(std::lround(middle)), .raised = path->elevation.has_value() };
        // A queue on the ground stores a slope of its own, but lies on the land as a ground path does.
        const std::uint8_t slope = path->elevation ? path->elevation->slope : 0;
        const bool sloped = slope != 0 && slope != save::kSlopeOnGround;
        const bool queue = path->kind == save::PathKind::Queue;
        const bool forbidden = queue ? rules.off_queues : rules.queues_only;
        if (positioning == Positioning::PathCentre)
        {
            spot.refused = sloped || forbidden;
            return spot;
        }
        // On a sloped deck an item kept to level paths may stand only at an end of the slope.
        const bool ends_only = rules.level_only && sloped;
        // The stored direction runs along columns (0, 1) or rows (2, 3); sides 1 and 3 face along columns.
        const auto at_end = [&](Side side) { return side.on_columns() == (path->direction < 2); };

        // A path's height at `side`: its edge there, or its middle where it has none, as along a queue's fences.
        const auto level = [&](const save::PathTile& p, Side side) -> std::optional<float> {
            if (const auto edge = edge_height(p, grid, side))
                return edge;
            float sum = 0;
            int count = 0;
            for (const Side s : kSides)
                if (const auto edge = edge_height(p, grid, s))
                {
                    sum += *edge;
                    ++count;
                }
            return count > 0 ? std::optional(sum / static_cast<float>(count)) : std::nullopt;
        };
        // Whether a path across `side` meets this one there, at about its height: joined, or the next lane of a queue.
        const auto joined = [&](Side side) {
            const auto here = level(*path, side);
            const save::Tile across = side.beyond(tile);
            return here && std::ranges::any_of(paths, [&](const save::PathTile& p) {
                       if (p.col != across.col || p.row != across.row)
                           return false;
                       const auto there = level(p, side.opposite());
                       return there && std::abs(*there - *here) < kSameHeight;
                   });
        };
        const float in_x = fx - static_cast<float>(tile.col);
        const float in_z = fz - static_cast<float>(tile.row);
        const bool wants_joined = positioning == Positioning::PathSideJoin;
        const std::optional<Side> best = nearest_side({ in_x, in_z }, [&](Side side) { return joined(side) == wants_joined && (!ends_only || at_end(side)); });
        const std::optional<Side> any = nearest_side({ in_x, in_z }, [](Side) { return true; });
        spot.refused = !best || forbidden;
        const Side side = best.value_or(*any);
        spot.turn = turn_facing(side);
        if (const auto& edge = edges[static_cast<std::size_t>(side.value)])
            spot.height = static_cast<std::int32_t>(std::lround(*edge));
        return spot;
    }

    bool furniture_taken(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::PlacedObject& item, Positioning positioning)
    {
        const bool middle = positioning == Positioning::PathCentre;
        return std::ranges::any_of(objects, [&](const save::PlacedObject& o) {
            if (o.hidden || o.col != item.col || o.row != item.row || std::abs(o.base_height.value_or(0) - item.base_height.value_or(0)) > kSameLevel)
                return false;
            const auto model = models.find(model_ref(o));
            if (model == models.end())
                return false;
            const auto theirs = static_cast<Positioning>(model->second.positioning);
            if (!on_paths(theirs) || (theirs == Positioning::PathCentre) != middle)
                return false;
            return middle || (o.rotation & 3) == (item.rotation & 3);
        });
    }

} // namespace rct3::park
