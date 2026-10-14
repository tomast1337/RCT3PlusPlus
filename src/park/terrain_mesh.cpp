// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/terrain_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace rct3::park
{

    namespace
    {

        const glm::vec3 kSun = glm::normalize(glm::vec3(-0.4f, 1.0f, -0.3f));
        constexpr float kAmbient = 0.45f; ///< Light on a face turned away from the sun

        using Key = std::pair<GroundPaint, std::uint8_t>;
        using Triangle = std::array<glm::vec3, 3>;

        float shade(const Triangle& t)
        {
            const glm::vec3 n = glm::cross(t[1] - t[0], t[2] - t[0]);
            const float length = glm::length(n);
            if (length <= 0)
                return 1;
            const float lit = std::abs(glm::dot(n / length, kSun));
            return kAmbient + (1 - kAmbient) * lit;
        }

        /// Collects triangles per paint, then lays them out batch by batch.
        struct Builder
        {
            std::map<Key, std::vector<GroundVertex>> by_paint;

            void ground(Key key, const Triangle& t)
            {
                const float s = shade(t);
                auto& out = by_paint[key];
                for (const glm::vec3& p : t)
                    out.push_back({ p, { s, s, s, 1 }, { p.x / kTextureSpan, p.z / kTextureSpan } });
            }

            /// A wall running along x or along z, which picks where the texture's u comes from.
            void wall(Key key, const std::array<glm::vec3, 4>& quad, bool along_x)
            {
                const Triangle a { quad[0], quad[1], quad[2] };
                const Triangle b { quad[0], quad[2], quad[3] };
                for (const Triangle& t : { a, b })
                {
                    const float s = shade(t);
                    auto& out = by_paint[key];
                    for (const glm::vec3& p : t)
                        out.push_back({ p, { s, s, s, 1 }, { (along_x ? p.x : p.z) / kTextureSpan, -p.y / kTextureSpan } });
                }
            }
        };

        glm::vec3 corner(const save::TerrainGrid& g, int col, int row, int c)
        {
            const int dc = c % 2;
            const int dr = c / 2;
            return { g.world_x(col + dc), g.at(col, row).corners.at(static_cast<std::size_t>(c)), g.world_z(row + dr) };
        }

        /// The wall between two edges that share their ends in plan: ours (a0, a1) and the neighbour's
        /// (b0, b1). Nothing when they meet.
        void edge_wall(Builder& b, glm::vec3 a0, glm::vec3 a1, glm::vec3 b0, glm::vec3 b1, std::uint8_t cliff_a, std::uint8_t cliff_b, bool along_x)
        {
            if (std::abs(a0.y - b0.y) < kFlat && std::abs(a1.y - b1.y) < kFlat)
                return;
            // The higher side's cliff face shows.
            const std::uint8_t cliff = a0.y + a1.y >= b0.y + b1.y ? cliff_a : cliff_b;
            b.wall({ GroundPaint::Cliff, cliff }, { a0, a1, b1, b0 }, along_x);
        }

    } // namespace

    TerrainMesh terrain_mesh(const save::TerrainGrid& grid, const save::WaterLayer& water)
    {
        TerrainMesh mesh;
        if (grid.empty() || grid.cells.size() != static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height))
            return mesh;

        Builder b;
        float low = grid.cells.front().corners[0];
        float high = low;
        for (int row = 0; row < grid.height; ++row)
        {
            for (int col = 0; col < grid.width; ++col)
            {
                const save::TerrainCell& cell = grid.at(col, row);
                low = std::min(low, cell.min_height());
                high = std::max(high, cell.max_height());
                const std::array<glm::vec3, 4> c { corner(grid, col, row, 0), corner(grid, col, row, 1), corner(grid, col, row, 2), corner(grid, col, row, 3) };
                const Key surface { GroundPaint::Surface, cell.surface };
                b.ground(surface, { c[0], c[1], c[2] });
                b.ground(surface, { c[3], c[2], c[1] });
                if (grid.contains(col + 1, row))
                    edge_wall(b, c[1], c[3], corner(grid, col + 1, row, 0), corner(grid, col + 1, row, 2), cell.cliff, grid.at(col + 1, row).cliff, false);
                if (grid.contains(col, row + 1))
                    edge_wall(b, c[2], c[3], corner(grid, col, row + 1, 0), corner(grid, col, row + 1, 1), cell.cliff, grid.at(col, row + 1).cliff, true);
            }
        }

        for (auto& [key, vertices] : b.by_paint)
        {
            GroundBatch batch { key.first, key.second, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(vertices.size()) };
            for (GroundVertex& v : vertices)
            {
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }
            mesh.batches.push_back(batch);
        }

        for (const save::WaterBody& body : water.bodies)
        {
            for (const save::WaterTri& t : body.tris)
            {
                if (!grid.contains(t.col, t.row) || t.triangle > 1)
                    continue;
                const std::array<int, 3> corners = t.triangle == 0 ? std::array<int, 3> { 0, 1, 2 } : std::array<int, 3> { 3, 2, 1 };
                for (const int c : corners)
                {
                    glm::vec3 p = corner(grid, t.col, t.row, c);
                    p.y = body.level;
                    mesh.water.push_back({ p, kWaterColour });
                }
                high = std::max(high, body.level);
            }
        }

        mesh.low = { grid.world_x(0), low, grid.world_z(0) };
        mesh.high = { grid.world_x(grid.width), high, grid.world_z(grid.height) };
        return mesh;
    }

} // namespace rct3::park
