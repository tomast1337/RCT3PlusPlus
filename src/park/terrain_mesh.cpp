// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/terrain_mesh.hpp"

#include "park/lighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace rct3::park
{

    float surface_share(const save::TerrainGrid& g, int x, int z, std::uint8_t surface)
    {
        int touching = 0;
        int with = 0;
        for (int row = z - 1; row <= z; ++row)
        {
            for (int col = x - 1; col <= x; ++col)
            {
                if (!g.contains(col, row))
                    continue;
                ++touching;
                with += g.at(col, row).surface == surface ? 1 : 0;
            }
        }
        return touching > 0 ? static_cast<float>(with) / static_cast<float>(touching) : 0;
    }

    namespace
    {

        using Key = std::pair<GroundPaint, std::uint8_t>;
        using Triangle = std::array<glm::vec3, 3>;

        float shade(const Triangle& t)
        {
            return sunlight(glm::cross(t[1] - t[0], t[2] - t[0]));
        }

        /// Collects triangles per paint, then lays them out batch by batch.
        struct Builder
        {
            std::map<Key, std::vector<GroundVertex>> by_paint;

            /// A ground triangle, each corner lit by its own (smoothed) normal.
            void ground(Key key, const Triangle& t, const std::array<glm::vec3, 3>& normals, const std::array<float, 3>& alpha)
            {
                auto& out = by_paint[key];
                for (std::size_t i = 0; i < t.size(); ++i)
                {
                    const float s = sunlight(normals[i]);
                    out.push_back({ t[i], { s, s, s, alpha[i] }, { t[i].x / kTextureSpan, t[i].z / kTextureSpan } });
                }
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

        /// A tile's ground, once per surface at its corners, lowest index first. Each layer goes over the
        /// ones before it with an opacity that leaves every corner showing its surfaces in proportion: the
        /// first is opaque, and a later one covers its share of all the layers so far.
        void ground_layers(Builder& b, const save::TerrainGrid& g, int col, int row, const std::array<glm::vec3, 4>& c, const std::array<glm::vec3, 4>& n)
        {
            std::array<std::uint8_t, 16> surfaces { };
            std::size_t count = 0;
            for (int dr = -1; dr <= 1; ++dr)
            {
                for (int dc = -1; dc <= 1; ++dc)
                {
                    if (!g.contains(col + dc, row + dr))
                        continue;
                    const std::uint8_t s = g.at(col + dc, row + dr).surface;
                    if (std::find(surfaces.begin(), surfaces.begin() + static_cast<std::ptrdiff_t>(count), s) == surfaces.begin() + static_cast<std::ptrdiff_t>(count))
                        surfaces[count++] = s;
                }
            }
            std::sort(surfaces.begin(), surfaces.begin() + static_cast<std::ptrdiff_t>(count));

            std::array<float, 4> so_far { };
            for (std::size_t i = 0; i < count; ++i)
            {
                std::array<float, 4> alpha { };
                for (int k = 0; k < 4; ++k)
                {
                    const auto ku = static_cast<std::size_t>(k);
                    const float share = surface_share(g, col + k % 2, row + k / 2, surfaces[i]);
                    so_far[ku] += share;
                    alpha[ku] = i == 0 ? 1 : (so_far[ku] > 0 ? share / so_far[ku] : 0);
                }
                if (i > 0 && std::ranges::all_of(alpha, [](float a) { return a <= 0; }))
                    continue;
                const Key key { GroundPaint::Surface, surfaces[i] };
                b.ground(key, { c[0], c[1], c[2] }, { n[0], n[1], n[2] }, { alpha[0], alpha[1], alpha[2] });
                b.ground(key, { c[3], c[2], c[1] }, { n[3], n[2], n[1] }, { alpha[3], alpha[2], alpha[1] });
            }
        }

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

    glm::vec3 ground_normal(const save::TerrainGrid& g, int col, int row, int c)
    {
        const int x = col + c % 2;
        const int z = row + c / 2;
        const float height = g.at(col, row).corners.at(static_cast<std::size_t>(c));
        glm::vec3 sum { 0.0f };
        for (int r = z - 1; r <= z; ++r)
        {
            for (int q = x - 1; q <= x; ++q)
            {
                if (!g.contains(q, r))
                    continue;
                const int k = (x - q) + 2 * (z - r); // That grid point as the neighbour's corner
                if (std::abs(g.at(q, r).corners.at(static_cast<std::size_t>(k)) - height) >= kFlat)
                    continue;
                const std::array<glm::vec3, 4> p { corner(g, q, r, 0), corner(g, q, r, 1), corner(g, q, r, 2), corner(g, q, r, 3) };
                // Triangles (0, 1, 2) and (3, 2, 1): corners 1 and 2 are in both.
                if (k != 3)
                    sum += glm::cross(p[1] - p[0], p[2] - p[0]);
                if (k != 0)
                    sum += glm::cross(p[2] - p[3], p[1] - p[3]);
            }
        }
        return sum;
    }

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
                const std::array<glm::vec3, 4> n { ground_normal(grid, col, row, 0), ground_normal(grid, col, row, 1), ground_normal(grid, col, row, 2), ground_normal(grid, col, row, 3) };
                ground_layers(b, grid, col, row, c, n);
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

    void add_ground(TerrainMesh& mesh, const TerrainMesh& more)
    {
        if (more.indices.empty())
            return;
        const bool had = !mesh.indices.empty();
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        mesh.vertices.insert(mesh.vertices.end(), more.vertices.begin(), more.vertices.end());
        for (const std::uint32_t i : more.indices)
            mesh.indices.push_back(base + i);
        for (GroundBatch batch : more.batches)
        {
            batch.first += first;
            mesh.batches.push_back(batch);
        }
        mesh.low = had ? glm::min(mesh.low, more.low) : more.low;
        mesh.high = had ? glm::max(mesh.high, more.high) : more.high;
    }

} // namespace rct3::park
