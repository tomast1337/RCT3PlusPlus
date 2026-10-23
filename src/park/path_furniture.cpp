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
        /// Columns and rows to the tile across each side, as path_decks numbers the sides.
        constexpr std::array<std::array<int, 2>, 4> kSteps { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
        /// The quarter turns that face an item's +X at each side: faced_side turned back.
        constexpr std::array<std::int32_t, 4> kTurnsFacing { 1, 2, 3, 0 };
        constexpr float kSameHeight = 0.5f;    ///< World units between two edges that still meet
        constexpr std::int32_t kSameLevel = 2; ///< Units between two items' bases that still clash
    } // namespace

    std::optional<FurnitureSpot> furniture_spot(std::span<const save::PathTile> paths, const save::TerrainGrid& grid, glm::vec3 hit, Positioning positioning, std::int32_t turns)
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
            for (std::uint8_t side = 0; side < 4; ++side)
            {
                e[side] = edge_height(p, grid, side);
                if (e[side])
                {
                    sum += *e[side];
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
        FurnitureSpot spot { .tile = tile, .turns = turns & 3, .height = static_cast<std::int32_t>(std::lround(middle)), .raised = path->elevation.has_value() };
        if (positioning == Positioning::PathCentre)
            return spot;

        // Whether a path across `side` meets this one's edge there.
        const auto joined = [&](std::uint8_t side) {
            if (!edges[side])
                return false;
            const int col = tile.col + kSteps[side][0];
            const int row = tile.row + kSteps[side][1];
            return std::ranges::any_of(paths, [&](const save::PathTile& p) {
                if (p.col != col || p.row != row)
                    return false;
                const auto there = edge_height(p, grid, static_cast<std::uint8_t>((side + 2) % 4));
                return there && std::abs(*there - *edges[side]) < kSameHeight;
            });
        };
        const float in_x = fx - static_cast<float>(tile.col);
        const float in_z = fz - static_cast<float>(tile.row);
        const std::array<float, 4> away { 1 - in_z, 1 - in_x, in_z, in_x };
        const bool wants_joined = positioning == Positioning::PathSideJoin;
        std::optional<std::uint8_t> best;
        std::optional<std::uint8_t> any;
        for (std::uint8_t side = 0; side < 4; ++side)
        {
            if (!any || away[side] < away[*any])
                any = side;
            if (joined(side) == wants_joined && (!best || away[side] < away[*best]))
                best = side;
        }
        const std::uint8_t side = best.value_or(*any);
        spot.turns = kTurnsFacing[side];
        if (edges[side])
            spot.height = static_cast<std::int32_t>(std::lround(*edges[side]));
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
