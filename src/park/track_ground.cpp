// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_ground.hpp"

#include "park/boundary_fence.hpp"
#include "park/item_category.hpp"
#include "park/path_clearance.hpp"
#include "park/path_tunnels.hpp"
#include "park/water_fill.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace rct3::park
{

    std::vector<GroundCut> ground_cuts(std::span<const save::PlacedObject> items, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        constexpr float kSlack = 0.05f; // Standing on the ground is not in it
        std::vector<GroundCut> out;
        for (const auto& [key, spans] : solid_tiles(items, models, grid).tiles)
        {
            const save::Tile tile { static_cast<std::int32_t>(key & 0xFFFF), static_cast<std::int32_t>(key >> 16) };
            if (!grid.contains(tile.col, tile.row))
                continue;
            const save::TerrainCell& cell = grid.at(tile.col, tile.row);
            float floor = std::numeric_limits<float>::max();
            for (const SolidSpan& s : spans)
            {
                if (cell.max_height() > s.low + kSlack && cell.min_height() < s.high + kGroundCover)
                    floor = std::min(floor, s.low);
            }
            if (floor == std::numeric_limits<float>::max())
                continue;
            GroundCut cut { tile, cell.corners };
            for (float& corner : cut.corners)
                corner = std::min(corner, floor);
            out.push_back(cut);
        }
        return out;
    }

    bool cuts_refused(std::span<const GroundCut> cuts, const save::TerrainGrid& grid, const GroundHolds& holds, std::span<const save::WaterBody> bodies)
    {
        return std::ranges::any_of(cuts, [&](const GroundCut& cut) {
            const auto [col, row] = cut.tile;
            const auto wet = [&](std::int32_t c, std::int32_t r) { return holds_water(bodies, { c, r }); };
            return !owned_outright(grid.at(col, row)) || wet(col, row) || wet(col - 1, row) || wet(col + 1, row) || wet(col, row - 1) || wet(col, row + 1) || refuses(holds, cut.tile, grid.at(col, row).corners, cut.corners);
        });
    }

    std::vector<GroundOpening> track_hollows(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        std::vector<save::PlacedObject> track;
        std::ranges::copy_if(objects, std::back_inserter(track), [&](const save::PlacedObject& o) {
            const auto model = models.find(model_ref(o));
            return model != models.end() && model->second.category == std::to_underlying(ItemCategory::TrackPiece);
        });
        std::vector<GroundOpening> hollows;
        for (const auto& [key, spans] : solid_tiles(track, models, grid).tiles)
        {
            const auto col = static_cast<std::int32_t>(key & 0xFFFF);
            const auto row = static_cast<std::int32_t>(key >> 16);
            if (!grid.contains(col, row))
                continue;
            for (const SolidSpan& s : spans)
            {
                if (grid.at(col, row).min_height() >= s.high + kGroundCover)
                    hollows.push_back({ col, row, s.low - kTunnelFloor, s.high });
            }
        }
        return hollows;
    }

} // namespace rct3::park
