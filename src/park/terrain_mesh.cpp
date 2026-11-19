// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/terrain_mesh.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace rct3::park
{

    f32 surface_share(const save::model::TerrainGrid& g, i32 x, i32 z, u8 surface)
    {
        i32 touching = 0;
        i32 with = 0;
        for (i32 row = z - 1; row <= z; ++row)
        {
            for (i32 col = x - 1; col <= x; ++col)
            {
                if (!g.contains(col, row))
                    continue;
                ++touching;
                with += g.at(col, row).surface == surface ? 1 : 0;
            }
        }
        return touching > 0 ? static_cast<f32>(with) / static_cast<f32>(touching) : 0;
    }

    namespace
    {

        using Key = std::pair<GroundPaint, u8>;
        using Triangle = std::array<glm::vec3, 3>;

        glm::vec3 facing(const Triangle& t)
        {
            return glm::cross(t[1] - t[0], t[2] - t[0]);
        }

        /// Collects triangles per paint, then lays them out batch by batch.
        struct Builder
        {
            std::map<Key, std::vector<GroundVertex>> by_paint;

            /// A ground triangle, each corner with its own (smoothed) normal.
            void ground(Key key, const Triangle& t, const std::array<glm::vec3, 3>& normals, const std::array<f32, 3>& alpha)
            {
                auto& out = by_paint[key];
                for (usize i = 0; i < t.size(); ++i)
                {
                    out.push_back({ t[i], { 1, 1, 1, alpha[i] }, { t[i].x / kTextureSpan, t[i].z / kTextureSpan }, normals[i] });
                }
            }

            /// A wall running along x or along z, which picks where the texture's u comes from.
            void wall(Key key, const std::array<glm::vec3, 4>& quad, bool along_x)
            {
                const Triangle a { quad[0], quad[1], quad[2] };
                const Triangle b { quad[0], quad[2], quad[3] };
                for (const Triangle& t : { a, b })
                {
                    const glm::vec3 normal = facing(t);
                    auto& out = by_paint[key];
                    for (const glm::vec3& p : t)
                        out.push_back({ p, glm::vec4(1.0f), { (along_x ? p.x : p.z) / kTextureSpan, -p.y / kTextureSpan }, normal });
                }
            }
        };

        /// A tile's ground, once per surface at its corners, lowest index first. Each layer goes over the
        /// ones before it with an opacity that leaves every corner showing its surfaces in proportion: the
        /// first is opaque, and a later one covers its share of all the layers so far.
        void ground_layers(Builder& b, const save::model::TerrainGrid& g, i32 col, i32 row, const std::array<glm::vec3, 4>& c, const std::array<glm::vec3, 4>& n)
        {
            std::array<u8, 16> surfaces { };
            usize count = 0;
            for (i32 dr = -1; dr <= 1; ++dr)
            {
                for (i32 dc = -1; dc <= 1; ++dc)
                {
                    if (!g.contains(col + dc, row + dr))
                        continue;
                    const u8 s = g.at(col + dc, row + dr).surface;
                    if (std::find(surfaces.begin(), surfaces.begin() + static_cast<std::ptrdiff_t>(count), s) == surfaces.begin() + static_cast<std::ptrdiff_t>(count))
                        surfaces[count++] = s;
                }
            }
            std::sort(surfaces.begin(), surfaces.begin() + static_cast<std::ptrdiff_t>(count));

            std::array<f32, 4> so_far { };
            for (usize i = 0; i < count; ++i)
            {
                std::array<f32, 4> alpha { };
                for (i32 k = 0; k < 4; ++k)
                {
                    const auto ku = static_cast<usize>(k);
                    const f32 share = surface_share(g, col + k % 2, row + k / 2, surfaces[i]);
                    so_far[ku] += share;
                    alpha[ku] = i == 0 ? 1 : (so_far[ku] > 0 ? share / so_far[ku] : 0);
                }
                if (i > 0 && std::ranges::all_of(alpha, [](f32 a) { return a <= 0; }))
                    continue;
                const Key key { GroundPaint::Surface, surfaces[i] };
                b.ground(key, { c[0], c[1], c[2] }, { n[0], n[1], n[2] }, { alpha[0], alpha[1], alpha[2] });
                b.ground(key, { c[3], c[2], c[1] }, { n[3], n[2], n[1] }, { alpha[3], alpha[2], alpha[1] });
            }
        }

        glm::vec3 corner(const save::model::TerrainGrid& g, i32 col, i32 row, i32 c)
        {
            const i32 dc = c % 2;
            const i32 dr = c / 2;
            return { g.world_x(col + dc), g.at(col, row).corners.at(static_cast<usize>(c)), g.world_z(row + dr) };
        }

        /// The wall between two edges that share their ends in plan: ours (a0, a1) and the neighbour's
        /// (b0, b1). Nothing when they meet.
        void edge_wall(Builder& b, glm::vec3 a0, glm::vec3 a1, glm::vec3 b0, glm::vec3 b1, u8 cliff_a, u8 cliff_b, bool along_x)
        {
            if (std::abs(a0.y - b0.y) < kFlat && std::abs(a1.y - b1.y) < kFlat)
                return;
            // The higher side's cliff face shows.
            const u8 cliff = a0.y + a1.y >= b0.y + b1.y ? cliff_a : cliff_b;
            b.wall({ GroundPaint::Cliff, cliff }, { a0, a1, b1, b0 }, along_x);
        }

        /// The same wall without the bands in `open`: each part between them, its ends' heights held to it.
        /// shortcut: a sloping wall's part is cut straight at its ends, not where its slope crosses a band's
        /// edge; fine for openings on level walls, cut exactly if sloping ones show gaps.
        void open_wall(Builder& b, glm::vec3 a0, glm::vec3 a1, glm::vec3 b0, glm::vec3 b1, u8 cliff_a, u8 cliff_b, bool along_x, std::vector<std::pair<f32, f32>> open)
        {
            if (open.empty())
                return edge_wall(b, a0, a1, b0, b1, cliff_a, cliff_b, along_x);
            std::ranges::sort(open);
            const u8 cliff = a0.y + a1.y >= b0.y + b1.y ? cliff_a : cliff_b;
            f32 from = -std::numeric_limits<f32>::infinity();
            const auto part = [&](f32 low, f32 high) {
                if (high - low < kFlat)
                    return;
                const auto held = [&](glm::vec3 p) { return glm::vec3 { p.x, std::clamp(p.y, low, high), p.z }; };
                edge_wall(b, held(a0), held(a1), held(b0), held(b1), cliff, cliff, along_x);
            };
            for (const auto& [low, high] : open)
            {
                part(from, low);
                from = std::max(from, high);
            }
            part(from, std::numeric_limits<f32>::infinity());
        }

    } // namespace

    glm::vec3 ground_normal(const save::model::TerrainGrid& g, save::model::Tile tile, i32 c)
    {
        const auto [col, row] = tile;
        const i32 x = col + c % 2;
        const i32 z = row + c / 2;
        const f32 height = g.at(col, row).corners.at(static_cast<usize>(c));
        glm::vec3 sum { 0.0f };
        for (i32 r = z - 1; r <= z; ++r)
        {
            for (i32 q = x - 1; q <= x; ++q)
            {
                if (!g.contains(q, r))
                    continue;
                const i32 k = (x - q) + 2 * (z - r); // That grid point as the neighbour's corner
                if (std::abs(g.at(q, r).corners.at(static_cast<usize>(k)) - height) >= kFlat)
                    continue;
                const std::array<glm::vec3, 4> p { corner(g, q, r, 0), corner(g, q, r, 1), corner(g, q, r, 2), corner(g, q, r, 3) };
                // Triangles (0, 1, 2) and (3, 2, 1): corners 1 and 2 are in both. Crossed so they point up.
                if (k != 3)
                    sum += glm::cross(p[2] - p[0], p[1] - p[0]);
                if (k != 0)
                    sum += glm::cross(p[1] - p[3], p[2] - p[3]);
            }
        }
        return sum;
    }

    TerrainMesh terrain_mesh(const save::model::TerrainGrid& grid, const save::model::WaterLayer& water, std::span<const GroundOpening> openings, std::optional<TileRect> area)
    {
        TerrainMesh mesh;
        if (grid.empty() || grid.cells.size() != static_cast<usize>(grid.width) * static_cast<usize>(grid.height))
            return mesh;
        std::map<std::pair<i32, i32>, std::vector<std::pair<f32, f32>>> open_at;
        for (const GroundOpening& o : openings)
            open_at[{ o.col, o.row }].emplace_back(o.low, o.high);
        // The bands open on the edge between two tiles: either tile's.
        const auto open_between = [&](i32 col, i32 row, i32 other_col, i32 other_row) {
            std::vector<std::pair<f32, f32>> open;
            for (const auto& at : { std::pair { col, row }, std::pair { other_col, other_row } })
                if (const auto found = open_at.find(at); found != open_at.end())
                    open.insert(open.end(), found->second.begin(), found->second.end());
            return open;
        };

        const TileRect tiles = area.value_or(TileRect { 0, 0, grid.width, grid.height });
        const i32 first_col = std::max(tiles.col, 0);
        const i32 first_row = std::max(tiles.row, 0);
        if (first_col >= std::min(tiles.end_col, grid.width) || first_row >= std::min(tiles.end_row, grid.height))
            return mesh;
        Builder b;
        f32 low = grid.at(first_col, first_row).corners[0];
        f32 high = low;
        for (i32 row = first_row; row < std::min(tiles.end_row, grid.height); ++row)
        {
            for (i32 col = first_col; col < std::min(tiles.end_col, grid.width); ++col)
            {
                const save::model::TerrainCell& cell = grid.at(col, row);
                low = std::min(low, cell.min_height());
                high = std::max(high, cell.max_height());
                const std::array<glm::vec3, 4> c { corner(grid, col, row, 0), corner(grid, col, row, 1), corner(grid, col, row, 2), corner(grid, col, row, 3) };
                const std::array<glm::vec3, 4> n { ground_normal(grid, { col, row }, 0), ground_normal(grid, { col, row }, 1), ground_normal(grid, { col, row }, 2), ground_normal(grid, { col, row }, 3) };
                ground_layers(b, grid, col, row, c, n);
                if (grid.contains(col + 1, row))
                    open_wall(b, c[1], c[3], corner(grid, col + 1, row, 0), corner(grid, col + 1, row, 2), cell.cliff_style, grid.at(col + 1, row).cliff_style, false, open_between(col, row, col + 1, row));
                if (grid.contains(col, row + 1))
                    open_wall(b, c[2], c[3], corner(grid, col, row + 1, 0), corner(grid, col, row + 1, 1), cell.cliff_style, grid.at(col, row + 1).cliff_style, true, open_between(col, row, col, row + 1));
            }
        }

        for (auto& [key, vertices] : b.by_paint)
        {
            GroundBatch batch { key.first, key.second, static_cast<u32>(mesh.indices.size()), static_cast<u32>(vertices.size()) };
            for (GroundVertex& v : vertices)
            {
                mesh.indices.push_back(static_cast<u32>(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }
            mesh.batches.push_back(batch);
        }

        for (const save::model::WaterBody& body : water.bodies)
        {
            for (const save::model::WaterTri& t : body.tris)
            {
                if (!grid.contains(t.col, t.row) || !tiles.contains(t.col, t.row) || t.triangle > 1)
                    continue;
                const std::array<i32, 3> corners = t.triangle == 0 ? std::array<i32, 3> { 0, 1, 2 } : std::array<i32, 3> { 3, 2, 1 };
                for (const i32 c : corners)
                {
                    glm::vec3 p = corner(grid, t.col, t.row, c);
                    const f32 depth = std::max(body.level - p.y, 0.0f);
                    p.y = body.level;
                    mesh.water.push_back({ p, kWaterColour, depth });
                }
                high = std::max(high, body.level);
            }
        }

        mesh.low = { grid.world_x(first_col), low, grid.world_z(first_row) };
        mesh.high = { grid.world_x(std::min(tiles.end_col, grid.width)), high, grid.world_z(std::min(tiles.end_row, grid.height)) };
        return mesh;
    }

    TerrainMesh only_tiles(const TerrainMesh& mesh, const save::model::TerrainGrid& grid, const std::vector<bool>& keep)
    {
        TerrainMesh out;
        const auto kept = [&](usize first) {
            glm::vec3 middle { 0.0f };
            for (usize i = 0; i < 3; ++i)
                middle += mesh.vertices[mesh.indices[first + i]].position / 3.0f;
            const auto col = static_cast<i32>(std::floor((middle.x - grid.origin_x) / grid.tile_size_x));
            const auto row = static_cast<i32>(std::floor((middle.z - grid.origin_z) / grid.tile_size_z));
            return grid.contains(col, row) && keep[static_cast<usize>(row) * static_cast<usize>(grid.width) + static_cast<usize>(col)];
        };
        for (GroundBatch batch : mesh.batches)
        {
            const auto first = static_cast<u32>(out.indices.size());
            for (usize at = batch.first; at + 3 <= usize { batch.first } + batch.count; at += 3)
            {
                if (!kept(at))
                    continue;
                for (usize i = 0; i < 3; ++i)
                {
                    out.indices.push_back(static_cast<u32>(out.vertices.size()));
                    out.vertices.push_back(mesh.vertices[mesh.indices[at + i]]);
                }
            }
            batch.first = first;
            batch.count = static_cast<u32>(out.indices.size()) - first;
            if (batch.count > 0)
                out.batches.push_back(batch);
        }
        return out;
    }

    void add_ground(TerrainMesh& mesh, const TerrainMesh& more)
    {
        if (more.indices.empty())
            return;
        const bool had = !mesh.indices.empty();
        const auto base = static_cast<u32>(mesh.vertices.size());
        const auto first = static_cast<u32>(mesh.indices.size());
        mesh.vertices.insert(mesh.vertices.end(), more.vertices.begin(), more.vertices.end());
        for (const u32 i : more.indices)
            mesh.indices.push_back(base + i);
        for (GroundBatch batch : more.batches)
        {
            batch.first += first;
            mesh.batches.push_back(batch);
        }
        mesh.low = had ? glm::min(mesh.low, more.low) : more.low;
        mesh.high = had ? glm::max(mesh.high, more.high) : more.high;
    }

    std::vector<WaterVertex> pool_water(const save::model::TerrainGrid& grid, std::span<const save::model::PoolWater> pools)
    {
        std::vector<WaterVertex> out;
        for (const save::model::PoolWater& pool : pools)
        {
            if (!grid.contains(pool.col, pool.row))
                continue;
            // In small squares, so the waves in it can move the surface, each knowing how far its wall is.
            const auto water = [&](i32 dc, i32 dr) { return std::ranges::any_of(pools, [&](const save::model::PoolWater& o) { return o.col == pool.col + dc && o.row == pool.row + dr && o.surface == pool.surface; }); };
            const f32 level = static_cast<f32>(pool.surface) + kPoolBrim;
            const glm::vec3 low = corner(grid, pool.col, pool.row, 0);
            const glm::vec3 high = corner(grid, pool.col, pool.row, 3);
            for (i32 i = 0; i < kPoolWaterSteps; ++i)
                for (i32 j = 0; j < kPoolWaterSteps; ++j)
                    for (const auto& [di, dj] : { std::pair { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 0, 1 }, { 1, 0 } })
                    {
                        const glm::vec2 t = glm::vec2 { i + di, j + dj } / static_cast<f32>(kPoolWaterSteps);
                        const glm::vec3 p { glm::mix(low.x, high.x, t.x), level, glm::mix(low.z, high.z, t.y) };
                        out.push_back({ p, kPoolColour, static_cast<f32>(pool.depth) + kPoolBrim, pool_wall_room(grid, { p.x - low.x, p.z - low.z }, water) });
                    }
        }
        return out;
    }

    f32 pool_wall_room(const save::model::TerrainGrid& grid, glm::vec2 within, std::function_ref<bool(i32 dc, i32 dr)> water)
    {
        const glm::vec2 size { grid.tile_size_x, grid.tile_size_z };
        f32 room = std::max(size.x, size.y);
        for (i32 dr = -1; dr <= 1; ++dr)
            for (i32 dc = -1; dc <= 1; ++dc)
            {
                if ((dc == 0 && dr == 0) || water(dc, dr))
                    continue;
                const auto across = [](i32 d, f32 at, f32 side) { return d < 0 ? at : d > 0 ? side - at : 0.0f; };
                room = std::min(room, glm::length(glm::vec2 { across(dc, within.x, size.x), across(dr, within.y, size.y) }));
            }
        return room;
    }

} // namespace rct3::park
