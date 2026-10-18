// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lines the view menu lays over the ground: the tile grid, and contours where the ground crosses each step of
// height. Each line is a thin flat strip just over the ground, lying on the corners of its own tile, so it
// follows slopes and stops at cliffs as the ground does.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

namespace rct3::park
{

    /// Across a line, in world units.
    inline constexpr float kViewLineWidth = 0.06f;
    /// Height between contours, in world units.
    inline constexpr float kContourStep = 1.0f;

    /// A strip along every edge of every tile.
    [[nodiscard]] TerrainMesh grid_lines(const save::TerrainGrid& grid, glm::vec4 colour);
    /// A strip wherever the ground crosses a whole multiple of `step`, through each triangle of each tile.
    [[nodiscard]] TerrainMesh contour_lines(const save::TerrainGrid& grid, float step, glm::vec4 colour);

} // namespace rct3::park
