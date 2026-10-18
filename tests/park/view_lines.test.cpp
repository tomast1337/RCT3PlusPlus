// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/view_lines.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat_grid(int width, int height, float at)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.corners = { at, at, at, at };
        return g;
    }

    constexpr std::size_t kStripVertices = 4;
} // namespace

TEST_CASE("the grid draws each tile edge once, the map's far edges too")
{
    const save::TerrainGrid g = flat_grid(3, 2, 5);
    const TerrainMesh mesh = grid_lines(g, glm::vec4(1));
    // rows * (cols + 1) edges one way, cols * (rows + 1) the other.
    CHECK(mesh.vertices.size() == (2 * 4 + 3 * 3) * kStripVertices);
    CHECK(std::ranges::all_of(mesh.vertices, [](const GroundVertex& v) { return v.position.y > 5; }));
}

TEST_CASE("contours cross a slope at each step and leave flat ground bare")
{
    CHECK(contour_lines(flat_grid(2, 2, 3), 1, glm::vec4(1)).vertices.empty());

    save::TerrainGrid g = flat_grid(1, 1, 0);
    g.cells[0].corners = { 0, 0, 3, 3 }; // Rising 3 units toward the next row
    const TerrainMesh mesh = contour_lines(g, 1, glm::vec4(1));
    // Levels 1 and 2 through each of the tile's two triangles; level 3 is the top edge, which is no crossing.
    CHECK(mesh.vertices.size() == 4 * kStripVertices);
    for (const GroundVertex& v : mesh.vertices)
    {
        const float level = v.position.y - 0.03f;
        CHECK(level == doctest::Approx(std::round(level)).epsilon(0.001));
    }
}
