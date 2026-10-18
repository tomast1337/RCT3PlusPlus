// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_drag.hpp"

#include <algorithm>

namespace rct3::park
{

    std::vector<PathSpot> drag_area(PathSpot from, PathSpot to, const save::TerrainGrid& grid)
    {
        // The stroke stays within reach of where it began.
        to.col = std::clamp(to.col, from.col - kLongestStroke + 1, from.col + kLongestStroke - 1);
        to.row = std::clamp(to.row, from.row - kLongestStroke + 1, from.row + kLongestStroke - 1);
        std::vector<PathSpot> area;
        for (int row = std::min(from.row, to.row); row <= std::max(from.row, to.row); ++row)
        {
            for (int col = std::min(from.col, to.col); col <= std::max(from.col, to.col); ++col)
            {
                if (grid.contains(col, row))
                    area.push_back({ col, row });
            }
        }
        return area;
    }

    DragPlan plan_drag(std::span<const PathSpot> area, const save::TerrainGrid& grid, std::span<const save::PathTile> paths)
    {
        DragPlan plan;
        for (const PathSpot spot : area)
        {
            if (ground_path_at(paths, spot))
            {
                ++plan.kept;
                continue;
            }
            FootpathBuilder one;
            start_at(one, spot);
            const auto piece = next_piece(one, grid);
            if (piece && piece->ground && lay_problem(one, grid, paths) == LayProblem::None)
                plan.lay.push_back(spot);
            else
                ++plan.refused;
        }
        return plan;
    }

} // namespace rct3::park
