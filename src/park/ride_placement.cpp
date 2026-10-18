// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_placement.hpp"

#include "park/boundary_fence.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace rct3::park
{

    namespace
    {
        bool has_path(std::span<const save::PathTile> paths, std::int32_t col, std::int32_t row)
        {
            return std::ranges::any_of(paths, [&](const save::PathTile& p) { return p.col == col && p.row == row; });
        }

        SiteProblem tile_problem(std::int32_t col, std::int32_t row, std::int32_t height, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
        {
            if (!grid.contains(col, row) || !owned_outright(grid.at(col, row)))
                return SiteProblem::NotOwned;
            if (has_path(paths, col, row))
                return SiteProblem::Path;
            // shortcut: anything solid reaching over the base blocks, however high; give rides a height to
            // check against once their models' heights are read.
            if (const auto spans = solids.tiles.find(static_cast<std::uint32_t>(row) << 16 | static_cast<std::uint32_t>(col)); spans != solids.tiles.end())
            {
                if (std::ranges::any_of(spans->second, [&](const SolidSpan& s) { return s.high > static_cast<float>(height); }))
                    return SiteProblem::Blocked;
            }
            return SiteProblem::None;
        }
    } // namespace

    RideSite ride_site(std::int32_t col, std::int32_t row, std::array<std::int32_t, 2> squares, std::int32_t turns, const save::TerrainGrid& grid)
    {
        RideSite site;
        site.turns = turns & 3;
        site.span = site.turns % 2 == 0 ? squares : std::array { squares[1], squares[0] };
        site.col = col - (site.span[0] - 1) / 2;
        site.row = row - (site.span[1] - 1) / 2;
        if (grid.contains(col, row))
        {
            const auto& corners = grid.at(col, row).corners;
            site.height = static_cast<std::int32_t>(std::lround(std::accumulate(corners.begin(), corners.end(), 0.0f) / 4));
        }
        return site;
    }

    SiteProblem site_problem(const RideSite& site, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
    {
        for (std::int32_t r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (const SiteProblem p = tile_problem(c, r, site.height, grid, paths, solids); p != SiteProblem::None)
                    return p;
            }
        }
        return SiteProblem::None;
    }

    void level_ground(save::TerrainGrid& grid, const RideSite& site)
    {
        const auto height = static_cast<float>(site.height);
        // Every corner point of the footprint, in each of the up to four tiles that share it.
        for (std::int32_t r = site.row; r <= site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c <= site.col + site.span[0]; ++c)
            {
                for (std::int32_t dr = 0; dr < 2; ++dr)
                {
                    for (std::int32_t dc = 0; dc < 2; ++dc)
                    {
                        // The tile whose corner (1 - dc, 1 - dr) is this point; corners go (col,row),
                        // (col+1,row), (col,row+1), (col+1,row+1).
                        const std::int32_t tc = c - 1 + dc;
                        const std::int32_t tr = r - 1 + dr;
                        if (grid.contains(tc, tr))
                            grid.at(tc, tr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))] = height;
                    }
                }
            }
        }
    }

    DoorSpot door_spot(const RideSite& site, std::int32_t col, std::int32_t row)
    {
        const std::int32_t last_col = site.col + site.span[0] - 1;
        const std::int32_t last_row = site.row + site.span[1] - 1;
        const std::int32_t c = std::clamp(col, site.col, last_col);
        const std::int32_t r = std::clamp(row, site.row, last_row);
        // How far the tile is past (or, inside, short of) each side; the nearest side takes the door.
        const std::array<std::int32_t, 4> past {
            site.col - col, // -x side
            row - last_row, // +z side
            col - last_col, // +x side
            site.row - row, // -z side
        };
        const auto side = static_cast<std::size_t>(std::ranges::max_element(past) - past.begin());
        switch (side)
        {
            case 0:
                return { site.col - 1, r, 2 };
            case 1:
                return { c, last_row + 1, 3 };
            case 2:
                return { last_col + 1, r, 0 };
            default:
                return { c, site.row - 1, 1 };
        }
    }

    SiteProblem door_problem(const RideSite& site, const DoorSpot& door, std::optional<DoorSpot> taken, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
    {
        if (taken && taken->col == door.col && taken->row == door.row)
            return SiteProblem::Blocked;
        return tile_problem(door.col, door.row, site.height, grid, paths, solids);
    }

} // namespace rct3::park
