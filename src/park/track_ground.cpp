// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_ground.hpp"

#include "park/item_category.hpp"
#include "park/path_clearance.hpp"
#include "park/path_tunnels.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rct3::park
{

    GroundCuts ground_cuts(std::span<const save::PlacedObject> items, const ObjectModels& models, const save::TerrainGrid& grid, bool smooth)
    {
        constexpr float kSlack = 0.05f; // Standing on the ground is not in it
        GroundCuts out;
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
            out.footprint.push_back(cut);
        }
        if (!smooth)
            return out;
        // Each corner point of a cut tile, keyed row << 16 | col, at its lowest; the tiles round take it.
        std::unordered_map<std::uint32_t, float> points;
        const auto point = [](std::int32_t col, std::int32_t row) { return static_cast<std::uint32_t>(row) << 16 | static_cast<std::uint32_t>(col); };
        for (const GroundCut& cut : out.footprint)
        {
            for (std::size_t k = 0; k < 4; ++k)
            {
                const auto [it, added] = points.try_emplace(point(cut.tile.col + static_cast<std::int32_t>(k % 2), cut.tile.row + static_cast<std::int32_t>(k / 2)), cut.corners[k]);
                it->second = std::min(it->second, cut.corners[k]);
            }
        }
        std::unordered_set<std::uint32_t> seen;
        for (const GroundCut& cut : out.footprint)
            seen.insert(point(cut.tile.col, cut.tile.row));
        const std::size_t cuts = out.footprint.size();
        for (std::size_t i = 0; i < cuts; ++i)
        {
            for (std::int32_t dr = -1; dr <= 1; ++dr)
            {
                for (std::int32_t dc = -1; dc <= 1; ++dc)
                {
                    const save::Tile tile { out.footprint[i].tile.col + dc, out.footprint[i].tile.row + dr };
                    if (!grid.contains(tile.col, tile.row) || !seen.insert(point(tile.col, tile.row)).second)
                        continue;
                    GroundCut rim { tile, grid.at(tile.col, tile.row).corners };
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        if (const auto it = points.find(point(tile.col + static_cast<std::int32_t>(k % 2), tile.row + static_cast<std::int32_t>(k / 2))); it != points.end())
                            rim.corners[k] = std::min(rim.corners[k], it->second);
                    }
                    if (rim.corners != grid.at(tile.col, tile.row).corners)
                        out.rim.push_back(rim);
                }
            }
        }
        return out;
    }

    std::vector<GroundOpening> buried_hollows(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        constexpr std::array kHollowing { ItemCategory::TrackPiece, ItemCategory::Ride, ItemCategory::RideEntrance, ItemCategory::RideExit, ItemCategory::Stall, ItemCategory::ParkEntrance };
        std::vector<save::PlacedObject> track;
        std::ranges::copy_if(objects, std::back_inserter(track), [&](const save::PlacedObject& o) {
            const auto model = models.find(model_ref(o));
            return model != models.end() && std::ranges::contains(kHollowing, model->second.category, [](ItemCategory c) { return std::to_underlying(c); });
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
