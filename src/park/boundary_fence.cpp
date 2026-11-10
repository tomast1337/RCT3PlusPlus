// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/boundary_fence.hpp"

#include "park/ground/queues.hpp"
#include "park/queue_fences.hpp"

#include <unordered_set>

namespace rct3::park
{

    bool owned_outright(const save::model::TerrainCell& cell) noexcept
    {
        return cell.has(save::model::TileFlag::Owned) && !cell.has(save::model::TileFlag::RightsOnly);
    }

    glm::vec2 panel_lean(const save::model::TerrainGrid& grid, const save::model::PlacedObject& panel)
    {
        for (const Side side : ground::kSidesFromRowBefore)
            if (edge_turns(side) == ModelTurn::from(panel.rotation))
                return edge_lean(grid, { panel.col, panel.row }, side);
        return { };
    }

    BoundaryFence boundary_fence(const std::string& symbol, const std::string& overlay, const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, std::uint64_t last_id)
    {
        BoundaryFence out;
        if (symbol.empty())
            return out;
        const auto key = [&](int col, int row) { return static_cast<std::int64_t>(row) * grid.width + col; };
        std::unordered_set<std::int64_t> path_tiles;
        for (const save::model::PathTile& p : paths)
            path_tiles.insert(key(p.col, p.row));
        const auto owned = [&](int col, int row) { return grid.contains(col, row) && owned_outright(grid.at(col, row)); };
        const auto has_path = [&](int col, int row) { return grid.contains(col, row) && path_tiles.contains(key(col, row)); };
        for (int row = 0; row < grid.height; ++row)
        {
            for (int col = 0; col < grid.width; ++col)
            {
                if (!owned(col, row))
                    continue;
                for (const Side side : ground::kSidesFromRowBefore)
                {
                    const auto [dc, dr] = side.step();
                    if (owned(col + dc, row + dr) || (has_path(col, row) && has_path(col + dc, row + dr)))
                        continue;
                    save::model::PlacedObject& o = out.pieces.emplace_back();
                    o.id = ++last_id;
                    o.owner_ref = o.id;
                    o.fixture = true;
                    o.symbol = symbol;
                    o.overlay = overlay;
                    o.col = col;
                    o.row = row;
                    o.rotation = edge_turns(side).value;
                    if (const glm::vec2 lean = panel_lean(grid, o); lean != glm::vec2(0.0f))
                        out.leans.emplace(o.id, lean);
                }
            }
        }
        return out;
    }

} // namespace rct3::park
