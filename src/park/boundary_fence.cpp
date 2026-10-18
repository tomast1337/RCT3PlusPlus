// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/boundary_fence.hpp"

#include "park/ground_queues.hpp"
#include "park/queue_fences.hpp"

#include <unordered_set>

namespace rct3::park
{

    bool owned_outright(const save::TerrainCell& cell) noexcept
    {
        return cell.has(save::TileFlag::Owned) && !cell.has(save::TileFlag::RightsOnly);
    }

    BoundaryFence boundary_fence(const std::string& symbol, const std::string& overlay, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::uint64_t last_id)
    {
        BoundaryFence out;
        if (symbol.empty())
            return out;
        const auto key = [&](int col, int row) { return static_cast<std::int64_t>(row) * grid.width + col; };
        std::unordered_set<std::int64_t> path_tiles;
        for (const save::PathTile& p : paths)
            path_tiles.insert(key(p.col, p.row));
        const auto owned = [&](int col, int row) { return grid.contains(col, row) && owned_outright(grid.at(col, row)); };
        const auto has_path = [&](int col, int row) { return grid.contains(col, row) && path_tiles.contains(key(col, row)); };
        for (int row = 0; row < grid.height; ++row)
        {
            for (int col = 0; col < grid.width; ++col)
            {
                if (!owned(col, row))
                    continue;
                for (const TileSide side : { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left })
                {
                    const auto [dc, dr] = side_step(side);
                    if (owned(col + dc, row + dr) || (has_path(col, row) && has_path(col + dc, row + dr)))
                        continue;
                    save::PlacedObject& o = out.pieces.emplace_back();
                    o.id = ++last_id;
                    o.owner_ref = o.id;
                    o.symbol = symbol;
                    o.overlay = overlay;
                    o.col = col;
                    o.row = row;
                    o.rotation = edge_turns(side);
                    if (const glm::vec2 lean = edge_lean(grid, col, row, side); lean != glm::vec2(0.0f))
                        out.leans.emplace(o.id, lean);
                }
            }
        }
        return out;
    }

} // namespace rct3::park
