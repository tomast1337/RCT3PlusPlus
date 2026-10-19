// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/view_lines.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>

namespace rct3::park
{

    namespace
    {
        constexpr float kLift = 0.03f; ///< Over the ground, so the line is not lost in it.

        void strip(TerrainMesh& mesh, glm::vec3 a, glm::vec3 b, glm::vec4 colour)
        {
            ground_strip(mesh, a, b, colour, kViewLineWidth);
        }

        /// The tile's corners in world space, in the grid's corner order.
        std::array<glm::vec3, 4> corners(const save::TerrainGrid& grid, int col, int row)
        {
            const save::TerrainCell& c = grid.at(col, row);
            const float x0 = grid.world_x(col), x1 = grid.world_x(col + 1), z0 = grid.world_z(row), z1 = grid.world_z(row + 1);
            return { glm::vec3 { x0, c.corners[0], z0 }, glm::vec3 { x1, c.corners[1], z0 }, glm::vec3 { x0, c.corners[2], z1 }, glm::vec3 { x1, c.corners[3], z1 } };
        }
    } // namespace

    void ground_strip(TerrainMesh& mesh, glm::vec3 a, glm::vec3 b, glm::vec4 colour, float width)
    {
        const glm::vec2 along { b.x - a.x, b.z - a.z };
        const float length = glm::length(along);
        if (length <= 0)
            return;
        const glm::vec3 side = glm::vec3 { -along.y, 0, along.x } * (width / 2 / length);
        const glm::vec3 up { 0, kLift, 0 };
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const glm::vec3 p : { a + up + side, a + up - side, b + up - side, b + up + side })
            mesh.vertices.push_back({ .position = p, .colour = colour, .normal = { 0, 1, 0 } });
        for (const std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u })
            mesh.indices.push_back(first + i);
    }

    TerrainMesh grid_lines(const save::TerrainGrid& grid, glm::vec4 colour)
    {
        TerrainMesh mesh;
        for (int row = 0; row < grid.height; ++row)
        {
            for (int col = 0; col < grid.width; ++col)
            {
                const auto c = corners(grid, col, row);
                strip(mesh, c[0], c[1], colour);
                strip(mesh, c[0], c[2], colour);
                // The far edges of the map's last tiles, which no tile beyond them draws.
                if (col == grid.width - 1)
                    strip(mesh, c[1], c[3], colour);
                if (row == grid.height - 1)
                    strip(mesh, c[2], c[3], colour);
            }
        }
        return mesh;
    }

    TerrainMesh contour_lines(const save::TerrainGrid& grid, float step, glm::vec4 colour)
    {
        TerrainMesh mesh;
        if (step <= 0)
            return mesh;
        for (int row = 0; row < grid.height; ++row)
        {
            for (int col = 0; col < grid.width; ++col)
            {
                const auto c = corners(grid, col, row);
                for (const std::array<glm::vec3, 3> t : { std::array { c[0], c[1], c[2] }, std::array { c[3], c[2], c[1] } })
                {
                    const float low = std::min({ t[0].y, t[1].y, t[2].y });
                    const float high = std::max({ t[0].y, t[1].y, t[2].y });
                    // Each level strictly between the lowest and highest corners. A corner on a level counts as
                    // under it, so a level crosses exactly two edges, and ground lying flat on one draws no line.
                    for (float level = std::floor(low / step + 1) * step; level < high; level += step)
                    {
                        std::array<glm::vec3, 2> ends { };
                        std::size_t found = 0;
                        for (std::size_t e = 0; e < 3; ++e)
                        {
                            const glm::vec3 a = t[e], b = t[(e + 1) % 3];
                            if ((a.y <= level) == (b.y <= level) || found == 2)
                                continue;
                            ends[found++] = a + (b - a) * ((level - a.y) / (b.y - a.y));
                        }
                        if (found == 2)
                            strip(mesh, ends[0], ends[1], colour);
                    }
                }
            }
        }
        return mesh;
    }

} // namespace rct3::park
