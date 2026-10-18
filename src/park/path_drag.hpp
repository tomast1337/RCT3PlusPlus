// SPDX-License-Identifier: GPL-3.0-or-later
//
// Laying many paths in one stroke: the player presses on one tile and lets go on another, and every tile of
// the rectangle between them gets a path on the ground. A stroke along a row or column lays a straight run;
// one across both lays a plaza.
//
// A tile of the stroke is laid only where a single flat piece started on it would lie on the ground, as the
// path builder decides (footpath_builder.hpp): the park owns it, its ground is flat enough, and no path is in
// the way. Tiles that already have a path on the ground are kept and cost nothing; the rest are left out.
#pragma once

#include "park/footpath_builder.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace rct3::park
{

    /// The largest stroke, in tiles along each side: keeps one stroke from covering the whole park by mistake.
    inline constexpr int kLongestStroke = 64;

    /// What a stroke from one tile to another would do.
    struct DragPlan
    {
        std::vector<PathSpot> lay; ///< Tiles that get a new path
        std::size_t kept = 0;      ///< Tiles that already have one
        std::size_t refused = 0;   ///< Tiles that cannot have one
    };

    /// The tiles of the rectangle from `from` to `to` (both included), on `grid`, row by row.
    [[nodiscard]] std::vector<PathSpot> drag_area(PathSpot from, PathSpot to, const save::TerrainGrid& grid);

    /// What a stroke over `area` would do among `paths`.
    [[nodiscard]] DragPlan plan_drag(std::span<const PathSpot> area, const save::TerrainGrid& grid, std::span<const save::PathTile> paths);

} // namespace rct3::park
